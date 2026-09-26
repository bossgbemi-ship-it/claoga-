# Dry vocal takes for the null test

Drop your own dry vocal takes here as `.wav` (mono or stereo, any sample rate).
`Tests/null_test.sh` renders every file in this folder through OJU v1 and through
OJU 2.0 (with all new modules off) and fails if any output differs by even one bit.

Synthetic takes are generated automatically, so the test also runs with this folder empty.
