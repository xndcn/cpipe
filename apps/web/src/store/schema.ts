import Ajv from "ajv";

export const schemaValidator = new Ajv({
  allErrors: true,
  strict: false
});
