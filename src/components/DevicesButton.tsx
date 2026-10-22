"use client";

import { useEffect, useState } from "react";
import Icon from "./Icon";
import { loadDevices, onDevicesChange, openDevices } from "@/lib/device-client";

export default function DevicesButton() {
  const [online, setOnline] = useState(0);
  useEffect(() => {
    let active = true;
    const refresh = () => { void loadDevices().then((snapshot) => { if (active) setOnline(snapshot.devices.filter((device) => device.online).length); }).catch(() => {}); };
    refresh();
    const timer = window.setInterval(refresh, 5000);
    const remove = onDevicesChange(refresh);
    return () => { active = false; window.clearInterval(timer); remove(); };
  }, []);
  return <button type="button" onClick={() => openDevices()} className="header-button devices-header-button" aria-label="DSi devices"><Icon name="device" className="h-4 w-4" /><span className="devices-header-label">Devices</span>{online ? <span className="device-online-count">{online}</span> : null}</button>;
}
