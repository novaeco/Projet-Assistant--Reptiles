# Image assets (not committed)

This project intentionally avoids tracking binary assets (JPEG, PNG, etc.) in Git to keep pull requests free of binaries.

If you need UI images during development:
- Place them locally under `public/images/` with the expected filenames.
- Do **not** commit the binary assets; add them to your personal `.git/info/exclude` if needed.
- Prefer lightweight placeholder paths or conditional loading so builds succeed even when the files are missing.

When sharing assets with collaborators, use out-of-repo channels (artifact storage, issue attachments, or cloud buckets) and document the required filenames in code comments.
