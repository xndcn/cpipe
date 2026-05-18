import { useDeviceStore } from "../../store/device";

export function DevicePane() {
  const lastAck = useDeviceStore((state) => state.lastAck);
  const lastProfile = useDeviceStore((state) => state.lastProfile);
  const status = useDeviceStore((state) => state.status);

  return (
    <footer className="device-pane">
      <span>Runtime: {status}</span>
      <span>Ack: {lastAck === undefined ? "none" : String(lastAck.received ?? lastAck.ok)}</span>
      <span>
        Profile: {lastProfile === undefined ? "none" : String(lastProfile.type ?? "received")}
      </span>
    </footer>
  );
}
