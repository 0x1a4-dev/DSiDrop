import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import ts from 'typescript';

export function registerTypescript() {
  const require = createRequire(import.meta.url);
  const previous = require.extensions['.ts'];
  require.extensions['.ts'] = (module, filename) => {
    const compiled = ts.transpileModule(readFileSync(filename, 'utf8'), {
      compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022, esModuleInterop: true },
      fileName: filename,
    });
    module._compile(compiled.outputText, filename);
  };
  return {
    require,
    restore() {
      if (previous) require.extensions['.ts'] = previous;
      else delete require.extensions['.ts'];
    },
  };
}
