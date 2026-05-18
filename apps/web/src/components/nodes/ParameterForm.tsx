import Ajv from "ajv";
import { useMemo, useState } from "react";

export interface ManifestParam {
  default?: unknown;
  enum_values?: string[];
  name: string;
  range?: {
    max?: number;
    min?: number;
  };
  type: string;
}

export interface ParamValidationResult {
  message?: string;
  valid: boolean;
}

const ajv = new Ajv({ allErrors: true, strict: false });

function paramSchema(param: ManifestParam) {
  if (param.type === "enum") {
    return { enum: param.enum_values ?? [] };
  }
  if (param.type === "boolean" || param.type === "bool") {
    return { type: "boolean" };
  }
  if (param.type === "string") {
    return { type: "string" };
  }
  if (param.type === "array") {
    return { type: "array" };
  }
  return {
    maximum: param.range?.max,
    minimum: param.range?.min,
    type: "number"
  };
}

export function validateManifestParamValue(
  param: ManifestParam,
  value: unknown
): ParamValidationResult {
  const validate = ajv.compile(paramSchema(param));
  if (validate(value)) {
    return { valid: true };
  }
  return {
    message: validate.errors?.map((error) => error.message).join("; ") ?? "invalid value",
    valid: false
  };
}

function valueForParam(param: ManifestParam, values: Record<string, unknown>) {
  return values[param.name] ?? param.default ?? (param.type === "boolean" ? false : "");
}

function coerceValue(param: ManifestParam, raw: string, checked: boolean) {
  if (param.type === "boolean" || param.type === "bool") {
    return checked;
  }
  if (param.type === "number") {
    return Number(raw);
  }
  return raw;
}

export function ParameterForm({
  nodeId,
  onCommit,
  params,
  values
}: {
  nodeId: string;
  onCommit: (key: string, value: unknown) => void;
  params: ManifestParam[];
  values: Record<string, unknown>;
}) {
  const [errors, setErrors] = useState<Record<string, string>>({});
  const visibleParams = useMemo(() => params.filter((param) => param.name.length > 0), [params]);

  if (visibleParams.length === 0) {
    return <p className="parameter-form__empty">No live parameters</p>;
  }

  function commit(param: ManifestParam, raw: string, checked = false) {
    const value = coerceValue(param, raw, checked);
    const result = validateManifestParamValue(param, value);
    if (!result.valid) {
      setErrors((current) => ({ ...current, [param.name]: result.message ?? "invalid value" }));
      return;
    }
    setErrors((current) => {
      const next = { ...current };
      delete next[param.name];
      return next;
    });
    onCommit(param.name, value);
  }

  return (
    <form className="parameter-form" aria-label={`${nodeId} parameters`}>
      {visibleParams.map((param) => {
        const value = valueForParam(param, values);
        const error = errors[param.name];
        return (
          <label className="parameter-form__row" key={param.name}>
            <span className="parameter-form__label">{param.name}</span>
            {param.type === "enum" ? (
              <select
                name={param.name}
                onChange={(event) => commit(param, event.currentTarget.value)}
                value={String(value)}
              >
                {(param.enum_values ?? []).map((choice) => (
                  <option key={choice} value={choice}>
                    {choice}
                  </option>
                ))}
              </select>
            ) : param.type === "boolean" || param.type === "bool" ? (
              <input
                checked={Boolean(value)}
                name={param.name}
                onChange={(event) =>
                  commit(param, event.currentTarget.value, event.currentTarget.checked)
                }
                type="checkbox"
              />
            ) : (
              <input
                max={param.range?.max}
                min={param.range?.min}
                name={param.name}
                onChange={(event) => commit(param, event.currentTarget.value)}
                step="any"
                type={param.type === "number" ? "range" : "text"}
                value={String(value)}
              />
            )}
            {error === undefined ? null : <span className="parameter-form__error">{error}</span>}
          </label>
        );
      })}
    </form>
  );
}
