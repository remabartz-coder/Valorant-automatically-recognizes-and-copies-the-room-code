# Contributing

Use the Windows build and verification steps in README.md. Keep changes focused and
describe observable behavior, validation, and any remaining limitations.

OCR changes should include a small reproducible crop or synthetic case with the
expected text. Confirm ambiguous I/1 and O/0 from the source; do not infer ground
truth from the same recognizer being tested. Do not count repeated still images as
independent live frames or describe model confidence as measured accuracy.

Use `--no-output` for app verification. Automated checks must not send game input,
replace the real clipboard, or play speech. Desktop capture checks use only a
temporary synthetic window. See docs/VALIDATION.md for test scope.

Do not commit Cookie values, signed media URLs, settings, event logs, dumps or full
private recordings. Share the minimum crop needed to reproduce an issue and check
its contents before submission. Keep dependency license and source records current.
