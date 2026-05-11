// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

const fs = require("node:fs");
const Module = require("node:module");
const path = require("node:path");

Module.globalPaths.push(path.resolve(path.dirname(process.execPath), "..", "lib", "node_modules"));

const Ajv2020 = require("ajv/dist/2020").default;

const schemaPath = "schemas/node-v0.1.json";
const manifestPaths = ["src/cpipe/nodes/passthrough.json"];

const readJson = (jsonPath) => JSON.parse(fs.readFileSync(jsonPath, "utf8"));

const ajv = new Ajv2020({ allErrors: true, strict: true });
const validate = ajv.compile(readJson(schemaPath));

let failed = false;
for (const manifestPath of manifestPaths) {
    const valid = validate(readJson(manifestPath));
    if (valid) {
        continue;
    }

    failed = true;
    console.error(`${manifestPath} failed ${schemaPath}`);
    console.error(ajv.errorsText(validate.errors, { separator: "\n" }));
}

if (failed) {
    process.exit(1);
}
