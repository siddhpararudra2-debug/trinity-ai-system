# Trusted Fusion Worker Protocol

This directory documents the external desktop worker required for real Fusion STEP/STL/F3D export. The Linux API server generates and validates the Fusion script; a controlled Fusion 360 desktop add-in performs the export.

## Server configuration

Set a secret of at least 32 characters only on the API server and the trusted worker. Never commit it:

```bash
export TRINITY_FUSION_WORKER_SECRET='replace-with-a-random-32-byte-secret'
```

The worker uses these API calls:

```text
POST /api/fusion/jobs/{job_id}/claim
POST /api/fusion/jobs/{job_id}/artifacts
POST /api/fusion/jobs/{job_id}/complete
```

The claim request is:

```json
{"worker_id":"fusion-desktop-01","expires_in_seconds":300}
```

The response contains a short-lived HMAC token, the typed CAD specification, and the server-generated script artifact plus SHA-256 hash. The worker must verify that the script downloaded from the URL hashes to the advertised value before executing it.

## Worker rules

The add-in must use a dedicated Fusion user profile and create a new document for every job. It must never modify the operator’s active project, execute arbitrary code from a chat message, or access unrelated files. Accept only the typed specification and an allowlisted generator version/script hash. Use a job-scoped temporary directory and delete it after successful upload.

After creating the model, the worker should assert that expected named features exist, at least one solid body exists, and the requested export formats are allowed. It should use the Fusion API export manager available in the installed Fusion version for STEP, STL, and native Fusion archive output. Exact API method names must be checked against the installed Fusion API reference because Autodesk may change signatures between releases.

## Completion

Upload each export as multipart form data:

```text
file=<binary>
token=<claim token>
kind=fusion_step|fusion_stl|fusion_archive
```

Then complete the job with the artifact IDs and normalized validation checks:

```json
{
  "token":"<claim token>",
  "status":"ready",
  "artifact_ids":["artifact_..."],
  "validation_status":"passed",
  "fusion_version":"<version>",
  "addin_version":"<version>",
  "checks":[
    {"name":"solid-body","status":"passed","message":"One or more solid bodies created."}
  ]
}
```

A flight or manufacturing-facing workflow should require human approval even when the worker reports `passed`. Record the worker ID, Fusion version, add-in version, generator hash, artifact hashes, and operator approval in the job manifest.
