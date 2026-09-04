import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const generated = path.join(root, 'lib', 'api-zod', 'src', 'generated', 'api.ts');
let source = fs.readFileSync(generated, 'utf8');
source = source.replaceAll('zod.looseObject(', 'zod.object(');
source = source.replaceAll('zod.email()', 'zod.string().email()');
source = source.replaceAll(
  'zod.instanceof(File)',
  "zod.custom<File>((value) => typeof File !== 'undefined' && value instanceof File)",
);
source = source.replace(/\s+$/u, '\n');
fs.writeFileSync(generated, source);
const indexPath = path.join(root, 'lib', 'api-zod', 'src', 'index.ts');
fs.writeFileSync(indexPath, `export * from './generated/api';\nexport type {\n  RunVisionOcrBody as RunVisionOcrBodyType,\n  UploadFusionArtifactBody as UploadFusionArtifactBodyType,\n  UploadKicadArtifactBody as UploadKicadArtifactBodyType,\n} from './generated/types';\n`);

const apiClientIndexPath = path.join(root, 'lib', 'api-client-react', 'src', 'index.ts');
const apiClientCanonical = `export * from "./generated/api";\nexport * from "./generated/api.schemas";\nexport { setBaseUrl, setAuthTokenGetter } from "./custom-fetch";\nexport type { AuthTokenGetter } from "./custom-fetch";\n`;
if (fs.readFileSync(apiClientIndexPath, 'utf8') !== apiClientCanonical) {
  fs.writeFileSync(apiClientIndexPath, apiClientCanonical);
}


