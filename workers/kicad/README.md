# Trinity KiCad CLI Worker

This worker runs fixed KiCad 9 commands inside a job-scoped project directory. It generates ERC and DRC reports, Gerber layers, Excellon drill files, a schematic BOM, and a GLB 3D preview, then can upload the outputs to Trinity.

## Install

Install the same pinned KiCad major/minor version on the worker and confirm:

```bash
kicad-cli version
```

The worker does not execute commands supplied by a user. It constructs only the following commands:

```text
kicad-cli sch erc --exit-code-violations --format json --output validation/erc.json BOARD.kicad_sch
kicad-cli pcb drc --exit-code-violations --format json --output validation/drc.json BOARD.kicad_pcb
kicad-cli pcb export gerbers --output manufacturing/gerbers BOARD.kicad_pcb
kicad-cli pcb export drill --output manufacturing/drill BOARD.kicad_pcb
kicad-cli sch export bom --output manufacturing/bom.csv BOARD.kicad_sch
kicad-cli pcb export glb --output manufacturing/preview.glb BOARD.kicad_pcb
```

The ERC and DRC commands use KiCad’s violation exit-code option. A nonzero result is retained in `kicad-worker-result.json`; do not ship manufacturing files when ERC/DRC has failed unless a qualified engineer explicitly approves the exceptions.

## Local run

```bash
python workers/kicad/kicad_worker.py ./job-project \
  --schematic trinity_board.kicad_sch \
  --pcb trinity_board.kicad_pcb \
  --outputs gerbers,drill,bom,3d_preview
```

The worker writes reports and outputs below the job folder. To upload the outputs and complete an existing PCB job, set the server and secrets in the worker environment and use the synchronization flags:

```bash
export TRINITY_KICAD_WORKER_SECRET='replace-with-a-random-32-byte-secret'
# Set this only if the API server also requires TRINITY_API_KEY.
export TRINITY_API_KEY='production-api-key'
python workers/kicad/kicad_worker.py ./job-project \
  --schematic trinity_board.kicad_sch \
  --pcb trinity_board.kicad_pcb \
  --server https://trinity.example.com \
  --job-id pcb_<32-hex-characters>
```

Never pass secrets as command-line arguments in shared shell history; prefer an environment variable or secret manager.

## Production controls

Use a dedicated container or VM with a pinned KiCad image, no inbound network access, a non-root user, a read-only source mount, a writable job-scoped output directory, CPU/memory/time limits, and an outbound-only connection to the Trinity API. Preserve the exact KiCad version, command arguments, reports, output hashes, worker ID, and operator approval in the job manifest.

KiCad CLI is documented at [the official KiCad 9 command-line reference](https://docs.kicad.org/9.0/en/cli/cli.html). The generated output is an engineering artifact and still requires review of footprints, stackup, manufacturer constraints, ERC/DRC exceptions, and fabrication rules.
