export function cacheableBuildFiles(paths: string[]): string[];
export function buildIdForAssets(assets: { path: string; content: string | Uint8Array }[]): string;
export function cacheNameFor(version: string, buildId: string): string;
export function isObsoleteCache(name: string, active: string): boolean;
