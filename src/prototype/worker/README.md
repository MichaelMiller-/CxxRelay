# prototype.worker

Local JSON-lines protocol worker for the script-based browser chat prototype.
Protocol: one JSON object per stdin line; one JSON object per stdout line.
No diagnostics or arbitrary user data are written to stdout.