// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

const fs = require("node:fs");
const Module = require("node:module");
const path = require("node:path");

Module.globalPaths.push(path.resolve(path.dirname(process.execPath), "..", "lib", "node_modules"));

const Ajv2020 = require("ajv/dist/2020").default;

const validations = [
    {
        schemaPath: "schemas/node-v0.1.json",
        documentPaths: ["src/cpipe/nodes/passthrough.json"],
    },
    {
        schemaPath: "schemas/node-v0.2.json",
        documentPaths: ["src/cpipe/nodes/passthrough.json"],
    },
    {
        schemaPath: "schemas/pipeline-v0.2.json",
        documentPaths: ["tests/fixtures/min-pipeline.cpipe.json"],
    },
];

const readJson = (jsonPath) => JSON.parse(fs.readFileSync(jsonPath, "utf8"));

let failed = false;
for (const { schemaPath, documentPaths } of validations) {
    const ajv = new Ajv2020({ allErrors: true, strict: true });
    const validate = ajv.compile(readJson(schemaPath));

    for (const documentPath of documentPaths) {
        const valid = validate(readJson(documentPath));
        if (valid) {
            continue;
        }

        failed = true;
        console.error(`${documentPath} failed ${schemaPath}`);
        console.error(ajv.errorsText(validate.errors, { separator: "\n" }));
    }
}

if (failed) {
    process.exit(1);
}
