export function downloadWav(blob: Blob) {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a'); anchor.href = url; anchor.download = 'sonic-link-debug.wav'; anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
