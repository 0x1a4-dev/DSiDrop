import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  output: "standalone",
  outputFileTracingRoot: process.cwd(),
  outputFileTracingIncludes: {
    "/*": ["node_modules/7zip-bin/linux/*/7za"],
  },
  cacheComponents: true,
  partialPrefetching: true,
  serverExternalPackages: ["7zip-bin"],
  images: {
    remotePatterns: [
      {
        protocol: "https",
        hostname: "raw.githubusercontent.com",
        pathname: "/libretro-thumbnails/**",
      },
    ],
  },
};

export default nextConfig;
