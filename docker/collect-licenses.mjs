import { mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import path from "node:path";

const output = "licenses/npm";
mkdirSync(output, { recursive: true });
const packages = [];

function collect(directory) {
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    if (!entry.isDirectory() || entry.name.startsWith(".")) continue;
    const folder = path.join(directory, entry.name);
    if (entry.name.startsWith("@")) { collect(folder); continue; }
    let metadata;
    try { metadata = JSON.parse(readFileSync(path.join(folder, "package.json"), "utf8")); }
    catch { continue; }
    const name = `${metadata.name.replaceAll("/", "__")}@${metadata.version}`;
    const notices = readdirSync(folder, { withFileTypes: true })
      .filter((file) => file.isFile() && /^(licen[cs]e|copying|notice)(\.|$)/i.test(file.name));
    for (const notice of notices) {
      mkdirSync(path.join(output, name), { recursive: true });
      writeFileSync(path.join(output, name, notice.name), readFileSync(path.join(folder, notice.name)));
    }
    packages.push({ name: metadata.name, version: metadata.version, license: metadata.license ?? null });
    if (readdirSync(folder).includes("node_modules")) collect(path.join(folder, "node_modules"));
  }
}

collect("node_modules");
writeFileSync("licenses/npm-packages.json", JSON.stringify(packages.sort((a, b) => a.name.localeCompare(b.name)), null, 2) + "\n");
