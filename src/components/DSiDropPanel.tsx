import Image from "next/image";
import Icon from "./Icon";

export default function DSiDropPanel() {
  return (
    <div className="dsidrop-panel animate-rise">
      <section className="client-download-card" aria-labelledby="client-heading">
        <div className="client-art">
          <Image src="/brand/dsidrop-icon.png" alt="" width={96} height={96} unoptimized className="[image-rendering:pixelated]" />
        </div>
        <div className="client-download-copy">
          <p className="eyebrow">THE DSI APP</p>
          <h2 id="client-heading">DSiDrop</h2>
          <p>Latest release · Nintendo DSi</p>
          <a href="/downloads/DSiDrop.nds" download="DSiDrop.nds" className="upload-primary mt-6">
            <Icon name="download" className="h-4 w-4" />Download DSiDrop.nds
          </a>
        </div>
      </section>
    </div>
  );
}
