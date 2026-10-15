#include "window_capture.h"

#include <algorithm>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

namespace queuepilot {
namespace {

using namespace winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using DxgiAccess = ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;

BOOL CALLBACK collectWindow(HWND handle, LPARAM data) {
    if (!IsWindowVisible(handle) || GetWindow(handle, GW_OWNER)) return TRUE;
    DWORD process = 0;
    GetWindowThreadProcessId(handle, &process);
    if (process == GetCurrentProcessId()) return TRUE;
    const int length = GetWindowTextLengthW(handle);
    if (length <= 0) return TRUE;
    RECT bounds{};
    if (!GetWindowRect(handle, &bounds) || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        return TRUE;
    std::wstring title(static_cast<std::size_t>(length + 1), L'\0');
    const int copied = GetWindowTextW(handle, title.data(), length + 1);
    if (copied <= 0) return TRUE;
    auto& windows = *reinterpret_cast<QList<WindowInfo>*>(data);
    windows.push_back({QString::fromWCharArray(title.data(), copied), reinterpret_cast<quintptr>(handle)});
    return TRUE;
}

bool predominantlyBlack(const QImage& image) {
    int bright = 0;
    int sampled = 0;
    const int stepX = std::max(1, image.width() / 32);
    const int stepY = std::max(1, image.height() / 18);
    for (int y = 0; y < image.height(); y += stepY) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x += stepX) {
            const QRgb pixel = row[x];
            if (qRed(pixel) > 12 || qGreen(pixel) > 12 || qBlue(pixel) > 12) ++bright;
            ++sampled;
        }
    }
    return sampled > 0 && bright * 100 < sampled;
}

class Apartment final {
public:
    Apartment() { init_apartment(apartment_type::multi_threaded); }
    ~Apartment() { uninit_apartment(); }
};

}

struct WindowCapture::Impl {
    std::jthread worker;

    void run(std::stop_token stop, HWND window,
             std::function<void(VideoFrame)> onFrame,
             std::function<void(QString)> onStatus) {
        QString lastStatus;
        QString stage = QStringLiteral("initializing capture");
        const auto report = [&](const QString& message) {
            if (stop.stop_requested() || message == lastStatus) return;
            lastStatus = message;
            if (onStatus) onStatus(message);
        };

        try {
            Apartment apartment;
            if (!GraphicsCaptureSession::IsSupported()) {
                report(QStringLiteral("Windows Graphics Capture is unavailable on this Windows version."));
                return;
            }
            if (!IsWindow(window)) {
                report(QStringLiteral("The selected window no longer exists."));
                return;
            }

            stage = QStringLiteral("creating the graphics device");
            com_ptr<ID3D11Device> device;
            com_ptr<ID3D11DeviceContext> context;
            HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                device.put(), nullptr, context.put());
            if (FAILED(result)) {
                check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                    device.put(), nullptr, context.put()));
            }
            const auto dxgi = device.as<IDXGIDevice>();
            com_ptr<IInspectable> inspectable;
            check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
            const auto graphicsDevice = inspectable.as<IDirect3DDevice>();

            stage = QStringLiteral("creating the capture item for the selected window");
            const auto interop = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            GraphicsCaptureItem item{nullptr};
            check_hresult(interop->CreateForWindow(window, guid_of<GraphicsCaptureItem>(), put_abi(item)));
            SizeInt32 poolSize = item.Size();
            if (poolSize.Width <= 0 || poolSize.Height <= 0) {
                report(QStringLiteral("The selected window has no capturable area."));
                return;
            }
            stage = QStringLiteral("creating the frame pool");
            auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
                graphicsDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize);
            stage = QStringLiteral("starting the capture session");
            auto session = pool.CreateCaptureSession(item);
            session.IsCursorCaptureEnabled(false);
            session.StartCapture();
            stage = QStringLiteral("receiving captured frames");
            report(QStringLiteral("Window capture started."));

            com_ptr<ID3D11Texture2D> staging;
            D3D11_TEXTURE2D_DESC stagingDescription{};
            std::uint64_t sequence = 0;
            auto lastFrameAt = Clock::now();
            auto nextDiagnosticAt = lastFrameAt;
            auto darkSince = Clock::time_point{};

            while (!stop.stop_requested()) {
                const auto now = Clock::now();
                if (now >= nextDiagnosticAt) {
                    nextDiagnosticAt = now + std::chrono::milliseconds(500);
                    if (!IsWindow(window)) {
                        report(QStringLiteral("The captured window was closed."));
                        break;
                    }
                    if (IsIconic(window)) {
                        report(QStringLiteral("The captured window is minimized; restore it to resume fresh frames."));
                    } else if (now - lastFrameAt > std::chrono::seconds(2)) {
                        report(QStringLiteral("Window capture has stopped receiving frames."));
                    }
                }

                auto frame = pool.TryGetNextFrame();
                if (!frame) {
                    Sleep(4);
                    continue;
                }
                // Keep the freshest frame when OCR or GPU readback takes longer than a frame interval.
                for (int count = 0; count < 3; ++count) {
                    auto newer = pool.TryGetNextFrame();
                    if (!newer) break;
                    frame.Close();
                    frame = std::move(newer);
                }
                const auto receivedAt = Clock::now();
                const SizeInt32 content = frame.ContentSize();
                const auto surface = frame.Surface().as<DxgiAccess>();
                com_ptr<ID3D11Texture2D> texture;
                check_hresult(surface->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
                D3D11_TEXTURE2D_DESC description{};
                texture->GetDesc(&description);
                const int width = std::min(content.Width, static_cast<int>(description.Width));
                const int height = std::min(content.Height, static_cast<int>(description.Height));
                if (width <= 0 || height <= 0) {
                    frame.Close();
                    Sleep(4);
                    continue;
                }
                if (!staging || description.Width != stagingDescription.Width ||
                    description.Height != stagingDescription.Height ||
                    description.Format != stagingDescription.Format) {
                    staging = nullptr;
                    stagingDescription = description;
                    stagingDescription.Usage = D3D11_USAGE_STAGING;
                    stagingDescription.BindFlags = 0;
                    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    stagingDescription.MiscFlags = 0;
                    check_hresult(device->CreateTexture2D(&stagingDescription, nullptr, staging.put()));
                }
                context->CopyResource(staging.get(), texture.get());
                D3D11_MAPPED_SUBRESOURCE mapped{};
                check_hresult(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
                QImage image(static_cast<const uchar*>(mapped.pData), width, height,
                             static_cast<qsizetype>(mapped.RowPitch), QImage::Format_RGB32);
                image = image.copy();
                context->Unmap(staging.get(), 0);
                frame.Close();
                if (content.Width != poolSize.Width || content.Height != poolSize.Height) {
                    poolSize = content;
                    pool.Recreate(graphicsDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize);
                }
                if (stop.stop_requested()) break;
                lastFrameAt = Clock::now();
                if (IsIconic(window)) continue;
                if (predominantlyBlack(image)) {
                    if (darkSince == Clock::time_point{}) darkSince = lastFrameAt;
                    if (lastFrameAt - darkSince > std::chrono::seconds(1))
                        report(QStringLiteral("Captured frames are predominantly black; check the selected window."));
                } else {
                    darkSince = {};
                    report(QStringLiteral("Window capture is receiving frames."));
                }
                if (onFrame) onFrame(VideoFrame{std::move(image), ++sequence, receivedAt, 0});
            }
            session.Close();
            pool.Close();
        } catch (const hresult_error& exception) {
            report(QStringLiteral("Window capture failed while %1: %2 (0x%3)")
                .arg(stage)
                .arg(QString::fromStdWString(exception.message().c_str()))
                .arg(static_cast<quint32>(exception.code().value), 8, 16, QLatin1Char('0')));
        } catch (const std::exception& exception) {
            report(QStringLiteral("Window capture failed while %1: %2").arg(stage, QString::fromUtf8(exception.what())));
        }
    }
};

WindowCapture::WindowCapture() : impl_(std::make_unique<Impl>()) {}
WindowCapture::~WindowCapture() { stop(); }

QList<WindowInfo> WindowCapture::windows() {
    QList<WindowInfo> result;
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&result));
    std::sort(result.begin(), result.end(), [](const WindowInfo& left, const WindowInfo& right) {
        return left.title.compare(right.title, Qt::CaseInsensitive) < 0;
    });
    return result;
}

void WindowCapture::start(quintptr handle, std::function<void(VideoFrame)> frame,
                          std::function<void(QString)> status) {
    stop();
    impl_->worker = std::jthread([implementation = impl_.get(), handle,
                                 frame = std::move(frame), status = std::move(status)]
                                 (std::stop_token stop) mutable {
        implementation->run(stop, reinterpret_cast<HWND>(handle), std::move(frame), std::move(status));
    });
}

void WindowCapture::stop() {
    if (impl_->worker.joinable()) {
        impl_->worker.request_stop();
        impl_->worker.join();
    }
}

}
