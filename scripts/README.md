# Repository scripts

- `src/hello.ts` is a small workspace script example.
- `post-merge.sh` is a post-merge helper.
- `patch-zod-codegen.mjs` applies the repository’s Zod 3 compatibility adjustment after Orval generation.
- `tsconfig.json` and `package.json` define the script package typecheck/hello commands.

Do not edit generated API output by hand without also updating the codegen patch or the source contract.
