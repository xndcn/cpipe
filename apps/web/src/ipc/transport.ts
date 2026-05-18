export type TransportKind = "websocket";

export interface TransportEndpoint {
  kind: TransportKind;
  url: string;
}
