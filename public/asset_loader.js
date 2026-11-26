(() => {
  'use strict';

  const IDB_NAME = 'gtavcweb_vfs_v1';
  const IDB_VERSION = 1;
  const IDB_STORE = 'chunks';
  const ARCHIVE_MAGIC = 'GTAVCPK2';
  const MANIFEST_URL = '/chunks/manifest.json';
  const DOWNLOAD_CONCURRENCY = 6;

  const utf8Decoder = new TextDecoder('utf-8');

  let cachedManifest = null;
  let manifestPromise = null;

  // ============================================================================
  // IndexedDB Per-Chunk Cache (`gtavcweb_vfs_v1` / store `chunks` keyed by `sha256`)
  // ============================================================================
  function openChunkDb() {
    return new Promise((resolve, reject) => {
      if (typeof indexedDB === 'undefined') {
        reject(new Error('IndexedDB unavailable'));
        return;
      }
      const req = indexedDB.open(IDB_NAME, IDB_VERSION);
      req.onupgradeneeded = () => {
        const db = req.result;
        if (!db.objectStoreNames.contains(IDB_STORE)) {
          db.createObjectStore(IDB_STORE, { keyPath: 'sha256' });
        }
      };
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => reject(req.error || new Error('Failed to open IndexedDB'));
    });
  }

  async function idbGetChunk(sha256) {
    if (!sha256) return null;
    try {
      const db = await openChunkDb();
      return await new Promise((resolve) => {
        const tx = db.transaction(IDB_STORE, 'readonly');
        const req = tx.objectStore(IDB_STORE).get(sha256);
        req.onsuccess = () => resolve(req.result || null);
        req.onerror = () => resolve(null);
        tx.oncomplete = () => db.close();
      });
    } catch (_) {
      return null;
    }
  }

  async function idbGetAllKeys() {
    try {
      const db = await openChunkDb();
      return await new Promise((resolve) => {
        const tx = db.transaction(IDB_STORE, 'readonly');
        const req = tx.objectStore(IDB_STORE).getAllKeys();
        req.onsuccess = () => resolve(Array.isArray(req.result) ? req.result : []);
        req.onerror = () => resolve([]);
        tx.oncomplete = () => db.close();
      });
    } catch (_) {
      return [];
    }
  }

  async function idbPutChunk(record) {
    if (!record || !record.sha256 || !record.buffer) return false;
    try {
      const db = await openChunkDb();
      return await new Promise((resolve) => {
        const tx = db.transaction(IDB_STORE, 'readwrite');
        tx.objectStore(IDB_STORE).put(record);
        tx.oncomplete = () => {
          db.close();
          resolve(true);
        };
        tx.onerror = () => {
          db.close();
          resolve(false);
        };
        tx.onabort = () => {
          db.close();
          resolve(false);
        };
      });
    } catch (_) {
      return false;
    }
  }

  async function idbEvictStalePackChunks(packName, validShaSet) {
    if (!packName || !validShaSet) return;
    try {
      const db = await openChunkDb();
      await new Promise((resolve) => {
        const tx = db.transaction(IDB_STORE, 'readwrite');
        const store = tx.objectStore(IDB_STORE);
        const cursorReq = store.openCursor();
        cursorReq.onsuccess = () => {
          const cursor = cursorReq.result;
          if (!cursor) return;
          const val = cursor.value;
          if (val && val.packName === packName && !validShaSet.has(val.sha256)) {
            cursor.delete();
          }
          cursor.continue();
        };
        tx.oncomplete = () => {
          db.close();
          resolve();
        };
        tx.onerror = () => {
          db.close();
          resolve();
        };
      });
    } catch (_) {
      // Ignore IDB errors
    }
  }

  async function idbDeleteChunk(sha256) {
    if (!sha256) return;
    try {
      const db = await openChunkDb();
      await new Promise((resolve) => {
        const tx = db.transaction(IDB_STORE, 'readwrite');
        tx.objectStore(IDB_STORE).delete(sha256);
        tx.oncomplete = () => {
          db.close();
          resolve();
        };
        tx.onerror = () => {
          db.close();
          resolve();
        };
      });
    } catch (_) {
      // Ignore IDB errors
    }
  }

  async function idbClearAll() {
    try {
      const db = await openChunkDb();
      await new Promise((resolve) => {
        const tx = db.transaction(IDB_STORE, 'readwrite');
        tx.objectStore(IDB_STORE).clear();
        tx.oncomplete = () => {
          db.close();
          resolve();
        };
        tx.onerror = () => {
          db.close();
          resolve();
        };
      });
      return true;
    } catch (_) {
      return false;
    }
  }

  // ============================================================================
  // Crypto & Gzip Helpers
  // ============================================================================
  function bytesToHex(bytes) {
    let out = '';
    for (let i = 0; i < bytes.length; i++) {
      out += bytes[i].toString(16).padStart(2, '0');
    }
    return out;
  }

  async function verifySha256IfSupported(uint8OrBuf, expectedHex) {
    if (!expectedHex) return true;
    const subtle = globalThis.crypto && globalThis.crypto.subtle;
    if (!subtle || typeof subtle.digest !== 'function') {
      console.warn('[GtaAssetLoader] crypto.subtle unavailable; skipping SHA-256 verification.');
      return true;
    }
    const digestBuf = await subtle.digest('SHA-256', uint8OrBuf);
    const actualHex = bytesToHex(new Uint8Array(digestBuf));
    if (actualHex.toLowerCase() !== expectedHex.toLowerCase()) {
      throw new Error(`SHA-256 mismatch: expected ${expectedHex}, got ${actualHex}`);
    }
    return true;
  }

  async function decompressGzip(compressedBytes) {
    if (typeof DecompressionStream !== 'function') {
      throw new Error('Browser does not support DecompressionStream("gzip")');
    }
    const stream = new Blob([compressedBytes]).stream().pipeThrough(new DecompressionStream('gzip'));
    return await new Response(stream).arrayBuffer();
  }

  // ============================================================================
  // Manifest & GTAVCPK2 Incremental Streaming Unpacker
  // ============================================================================
  async function fetchManifest(forceRefresh = false) {
    if (cachedManifest && !forceRefresh) return cachedManifest;
    if (manifestPromise && !forceRefresh) return manifestPromise;

    manifestPromise = (async () => {
      const res = await fetch(MANIFEST_URL, { cache: 'no-cache' });
      if (!res.ok) {
        throw new Error(`Failed to fetch ${MANIFEST_URL} (HTTP ${res.status})`);
      }
      const json = await res.json();
      if (!json || json.format !== ARCHIVE_MAGIC || !json.core) {
        throw new Error('Invalid manifest.json schema (expected GTAVCPK2)');
      }
      cachedManifest = json;
      return json;
    })();

    try {
      return await manifestPromise;
    } finally {
      manifestPromise = null;
    }
  }

  function validateChunkHeader(arrayBuffer, chunkUrl) {
    if (!arrayBuffer || arrayBuffer.byteLength < 12) {
      throw new Error(`GTAVCPK2 chunk too small: ${chunkUrl}`);
    }
    const bytes = new Uint8Array(arrayBuffer);
    const magic = utf8Decoder.decode(bytes.subarray(0, 8));
    if (magic !== ARCHIVE_MAGIC) {
      throw new Error(`Invalid GTAVCPK2 magic "${magic}" in ${chunkUrl}`);
    }
    const view = new DataView(arrayBuffer);
    const recordCount = view.getUint32(8, true);
    return { bytes, view, recordCount };
  }

  async function fetchChunkNetwork(chunkMeta, onBytesProgress) {
    const res = await fetch(chunkMeta.url, { cache: 'default' });
    if (!res.ok) {
      throw new Error(`Failed to fetch asset chunk ${chunkMeta.url} (HTTP ${res.status})`);
    }

    let compressedBuf;
    if (res.body && typeof res.body.getReader === 'function') {
      const reader = res.body.getReader();
      const pieces = [];
      let total = 0;
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        if (value && value.byteLength > 0) {
          pieces.push(value);
          total += value.byteLength;
          if (onBytesProgress) onBytesProgress(total);
        }
      }
      const merged = new Uint8Array(total);
      let off = 0;
      for (let i = 0; i < pieces.length; i++) {
        merged.set(pieces[i], off);
        off += pieces[i].byteLength;
        pieces[i] = null;
      }
      compressedBuf = merged.buffer;
    } else {
      compressedBuf = await res.arrayBuffer();
      if (onBytesProgress) onBytesProgress(compressedBuf.byteLength);
    }

    await verifySha256IfSupported(compressedBuf, chunkMeta.sha256);
    return compressedBuf;
  }

  async function acquireCompressedChunk(packName, chunkMeta, onBytesProgress) {
    const cached = await idbGetChunk(chunkMeta.sha256);
    if (cached && cached.buffer instanceof ArrayBuffer) {
      if (!chunkMeta.compressedSize || cached.buffer.byteLength === chunkMeta.compressedSize) {
        if (onBytesProgress) onBytesProgress(cached.buffer.byteLength);
        return { buffer: cached.buffer, cacheHit: true };
      }
      await idbDeleteChunk(chunkMeta.sha256);
    }

    const compressedBuf = await fetchChunkNetwork(chunkMeta, onBytesProgress);
    await idbPutChunk({
      sha256: chunkMeta.sha256,
      packName,
      chunkUrl: chunkMeta.url,
      buffer: compressedBuf,
      compressedSize: chunkMeta.compressedSize || compressedBuf.byteLength,
      uncompressedSize: chunkMeta.uncompressedSize || 0,
      cachedAt: Date.now(),
    });
    return { buffer: compressedBuf, cacheHit: false };
  }

  function mountChunkRecordsToFS(FS, createdDirs, arrayBuffer, chunkMeta) {
    const { bytes, view, recordCount } = validateChunkHeader(arrayBuffer, chunkMeta.url);
    const totalChunkBytes = arrayBuffer.byteLength;
    let offset = 12;
    let lastPath = '';

    for (let i = 0; i < recordCount; i++) {
      if (offset + 2 > totalChunkBytes) {
        throw new Error(`Corrupt GTAVCPK2 chunk ${chunkMeta.url}: truncated path length at record ${i}`);
      }
      const pathLen = view.getUint16(offset, true);
      offset += 2;

      if (offset + pathLen + 12 > totalChunkBytes) {
        throw new Error(`Corrupt GTAVCPK2 chunk ${chunkMeta.url}: truncated metadata at record ${i}`);
      }
      const rawPath = utf8Decoder.decode(bytes.subarray(offset, offset + pathLen));
      offset += pathLen;

      const totalFileSize = view.getUint32(offset, true);
      const sliceOffset = view.getUint32(offset + 4, true);
      const sliceLen = view.getUint32(offset + 8, true);
      offset += 12;

      if (offset + sliceLen > totalChunkBytes) {
        throw new Error(`Corrupt GTAVCPK2 chunk ${chunkMeta.url}: truncated slice for ${rawPath}`);
      }
      if (sliceOffset + sliceLen > totalFileSize) {
        throw new Error(
          `Corrupt GTAVCPK2 slice bounds for ${rawPath}: offset=${sliceOffset} len=${sliceLen} total=${totalFileSize}`
        );
      }

      const cleanRel = rawPath.replace(/\\/g, '/').replace(/^\/+/, '');
      if (cleanRel) {
        const fullPath = '/' + cleanRel;
        const slashIdx = fullPath.lastIndexOf('/');
        if (slashIdx > 0) {
          const parentDir = fullPath.slice(0, slashIdx);
          if (!createdDirs.has(parentDir)) {
            FS.mkdirTree(parentDir);
            createdDirs.add(parentDir);
          }
        }

        if (sliceOffset === 0 && sliceLen === totalFileSize) {
          // CRITICAL: .slice() copies only this file's bytes so the ~22 MiB chunk buffer can be GC'd
          const copy = bytes.slice(offset, offset + sliceLen);
          FS.writeFile(fullPath, copy, { canOwn: true });
        } else {
          if (sliceOffset === 0) {
            const fullBuf = new Uint8Array(totalFileSize);
            FS.writeFile(fullPath, fullBuf, { canOwn: true });
          }
          const node = FS.lookupPath(fullPath).node;
          node.contents.set(bytes.subarray(offset, offset + sliceLen), sliceOffset);
        }
        lastPath = cleanRel;
      }

      offset += sliceLen;
    }

    return { recordCount, lastPath };
  }

  async function streamAndMountPack(FS, packName, meta, onProgress) {
    if (!FS || typeof FS.writeFile !== 'function' || typeof FS.mkdirTree !== 'function') {
      throw new Error('Emscripten FS instance is not ready');
    }
    if (!meta || !Array.isArray(meta.chunks) || meta.chunks.length === 0) {
      throw new Error(`Missing chunks metadata for pack "${packName}"`);
    }

    const chunks = meta.chunks;
    const chunkCount = chunks.length;
    const totalCompressed = meta.totalCompressedSize || 0;
    const totalUncompressed = meta.totalUncompressedSize || 0;
    const fileCount = meta.fileCount || 0;

    if (typeof onProgress === 'function') {
      onProgress({
        loadedBytes: 0,
        totalBytes: totalCompressed || totalUncompressed,
        phase: 'cache-check',
        cacheHit: false,
        pack: packName,
        chunkIndex: 0,
        chunkCount,
        fileCount,
      });
    }

    const validShaSet = new Set(chunks.map((c) => c.sha256).filter(Boolean));
    await idbEvictStalePackChunks(packName, validShaSet);

    try {
      FS.chdir('/');
    } catch (_) {
      // Ignore if already at root
    }
    try {
      FS.mkdirTree('/userfiles');
      FS.mkdirTree('/MP3');
    } catch (_) {
      // Ignore if directories already exist
    }

    const createdDirs = new Set(['/']);
    const perChunkLoaded = new Array(chunkCount).fill(0);
    const slotPromises = new Array(chunkCount).fill(null);
    let nextToPrefetch = 0;
    let mountedUncompressed = 0;
    let allCacheHits = true;
    let lastEmitTs = 0;

    function emitDownloadProgress(force) {
      if (typeof onProgress !== 'function') return;
      const now = performance.now();
      if (!force && now - lastEmitTs < 50) return;
      lastEmitTs = now;
      let loadedBytes = 0;
      for (let i = 0; i < perChunkLoaded.length; i++) {
        loadedBytes += perChunkLoaded[i];
      }
      onProgress({
        loadedBytes,
        totalBytes: Math.max(totalCompressed, loadedBytes),
        phase: 'download',
        cacheHit: false,
        pack: packName,
        chunkCount,
        fileCount,
      });
    }

    function schedulePrefetch(mountedIdx) {
      const limit = Math.min(chunkCount, mountedIdx + DOWNLOAD_CONCURRENCY);
      while (nextToPrefetch < limit) {
        const idx = nextToPrefetch++;
        const chunkMeta = chunks[idx];
        slotPromises[idx] = acquireCompressedChunk(packName, chunkMeta, (bytesLen) => {
          perChunkLoaded[idx] = bytesLen;
          emitDownloadProgress(false);
        });
      }
    }

    schedulePrefetch(0);

    for (let idx = 0; idx < chunkCount; idx++) {
      schedulePrefetch(idx);

      let acquired = await slotPromises[idx];
      slotPromises[idx] = null;

      let compressedBuf = acquired.buffer;
      let cacheHit = acquired.cacheHit;
      acquired = null;
      if (!cacheHit) allCacheHits = false;

      const chunkMeta = chunks[idx];
      let decompressedBuf = null;
      try {
        decompressedBuf = await decompressGzip(compressedBuf);
        compressedBuf = null;
        if (chunkMeta.uncompressedSize && decompressedBuf.byteLength !== chunkMeta.uncompressedSize) {
          throw new Error(
            `Decompressed size mismatch for ${chunkMeta.url}: expected ${chunkMeta.uncompressedSize}, got ${decompressedBuf.byteLength}`
          );
        }
        validateChunkHeader(decompressedBuf, chunkMeta.url);
      } catch (err) {
        compressedBuf = null;
        decompressedBuf = null;
        if (cacheHit) {
          console.warn(`[GtaAssetLoader] Corrupt cached chunk ${chunkMeta.url}, re-downloading:`, err);
          await idbDeleteChunk(chunkMeta.sha256);
          const freshBuf = await fetchChunkNetwork(chunkMeta, (bytesLen) => {
            perChunkLoaded[idx] = bytesLen;
            emitDownloadProgress(false);
          });
          await idbPutChunk({
            sha256: chunkMeta.sha256,
            packName,
            chunkUrl: chunkMeta.url,
            buffer: freshBuf,
            compressedSize: chunkMeta.compressedSize || freshBuf.byteLength,
            uncompressedSize: chunkMeta.uncompressedSize || 0,
            cachedAt: Date.now(),
          });
          decompressedBuf = await decompressGzip(freshBuf);
          validateChunkHeader(decompressedBuf, chunkMeta.url);
          cacheHit = false;
          allCacheHits = false;
        } else {
          throw err;
        }
      }

      const { lastPath } = mountChunkRecordsToFS(FS, createdDirs, decompressedBuf, chunkMeta);
      mountedUncompressed += decompressedBuf.byteLength;
      decompressedBuf = null;

      if (typeof onProgress === 'function') {
        onProgress({
          loadedBytes: mountedUncompressed,
          totalBytes: Math.max(totalUncompressed, mountedUncompressed),
          phase: 'unpack',
          cacheHit,
          pack: packName,
          chunkIndex: idx + 1,
          chunkCount,
          fileCount,
          currentFile: lastPath,
        });
      }

      // Yield to event loop so GC reclaims chunk buffers and keeps peak JS heap low
      await new Promise((r) => setTimeout(r, 0));
    }

    try {
      FS.chdir('/');
    } catch (_) {
      // Ignore
    }

    if (typeof onProgress === 'function') {
      onProgress({
        loadedBytes: totalUncompressed || mountedUncompressed,
        totalBytes: totalUncompressed || mountedUncompressed,
        phase: 'ready',
        cacheHit: allCacheHits,
        pack: packName,
        chunkIndex: chunkCount,
        chunkCount,
        fileCount,
      });
    }

    return {
      fileCount,
      chunkCount,
      uncompressedSize: mountedUncompressed,
      cacheHit: allCacheHits,
    };
  }

  const loadedPacks = new Set();
  const inFlightPacks = new Map();

  const GtaAssetLoader = {
    async getManifest(forceRefresh = false) {
      return await fetchManifest(forceRefresh);
    },

    getCachedManifestSync() {
      return cachedManifest;
    },

    async hasCachedPack(packName = 'core') {
      try {
        const manifest = await fetchManifest(false);
        const meta = manifest[packName];
        if (!meta || !Array.isArray(meta.chunks) || meta.chunks.length === 0) return false;
        const keys = new Set(await idbGetAllKeys());
        for (let i = 0; i < meta.chunks.length; i++) {
          const sha = meta.chunks[i] && meta.chunks[i].sha256;
          if (!sha || !keys.has(sha)) return false;
        }
        return true;
      } catch (_) {
        return false;
      }
    },

    async isPackCached(packName = 'core') {
      return await this.hasCachedPack(packName);
    },

    async loadPack(FS, packName = 'core', onProgress = null) {
      if (inFlightPacks.has(packName)) {
        return await inFlightPacks.get(packName);
      }
      const p = (async () => {
        if (typeof onProgress === 'function') {
          onProgress({
            loadedBytes: 0,
            totalBytes: 0,
            phase: 'manifest',
            cacheHit: false,
            pack: packName,
          });
        }
        const manifest = await fetchManifest(false);
        const meta = manifest[packName];
        if (!meta) return null;
        const res = await streamAndMountPack(FS, packName, meta, onProgress);
        loadedPacks.add(packName);
        return res;
      })();
      inFlightPacks.set(packName, p);
      try {
        return await p;
      } finally {
        inFlightPacks.delete(packName);
      }
    },

    async loadCore(FS, onProgress = null) {
      return await this.loadPack(FS, 'core', onProgress);
    },

    async loadCuts(FS, onProgress = null) {
      return await this.loadPack(FS, 'cuts', onProgress);
    },

    isPackMounted(packName) {
      return loadedPacks.has(packName);
    },

    async clearCache() {
      return await idbClearAll();
    },
  };

  // ============================================================================
  // GtaAudioStream — Lazy Segmented Radio & Mission Audio Streamer
  // ============================================================================
  let audioCtx = null;
  let audioUnlocked = false;
  let unlockListenersAttached = false;
  const streamSlots = new Map();

  function normalizeAudioKey(filename) {
    let clean = String(filename || '')
      .replace(/\\/g, '/')
      .trim()
      .toLowerCase();
    clean = clean.replace(/^(\.\/|\/)+/, '');
    if (clean.startsWith('audio/')) {
      clean = clean.slice('audio/'.length);
    }
    return clean;
  }

  function resolveAudioTrack(filename) {
    const norm = normalizeAudioKey(filename);
    if (!norm) return null;
    const base = norm.split('/').pop();
    const stem = base.replace(/\.[^.]+$/, '');
    const streams = cachedManifest && cachedManifest.audioStreams;

    if (streams) {
      const entry =
        streams[norm] ||
        streams[base] ||
        streams[`${stem}.adf`] ||
        streams[`${stem}.mp3`] ||
        null;
      if (entry) {
        if (Array.isArray(entry.segments) && entry.segments.length > 0) {
          return {
            url: entry.segments[0].url,
            segments: entry.segments,
            durationMs: entry.durationMs || 0,
          };
        }
        if (entry.url) {
          return {
            url: entry.url,
            segments: null,
            durationMs: entry.durationMs || 0,
          };
        }
      }
    }

    return {
      url: `/audio/${stem}.mp3`,
      segments: null,
      durationMs: 0,
    };
  }

  function findSegmentIndex(segments, posMs) {
    if (!Array.isArray(segments) || segments.length === 0) return 0;
    const pos = Math.max(0, Number(posMs) || 0);
    for (let i = 0; i < segments.length; i++) {
      const seg = segments[i];
      const start = seg.startMs || 0;
      const dur = seg.durationMs || 0;
      if (pos >= start && pos < start + dur) {
        return i;
      }
    }
    if (pos < (segments[0].startMs || 0)) return 0;
    return segments.length - 1;
  }

  function ensureAudioContext() {
    if (audioCtx) return audioCtx;
    const Ctx = globalThis.AudioContext || globalThis.webkitAudioContext;
    if (!Ctx) return null;
    try {
      audioCtx = new Ctx();
    } catch (_) {
      audioCtx = null;
    }
    return audioCtx;
  }

  function applySlotGainAndPan(slot) {
    const vol = Math.max(0, Math.min(1, slot.vol0to127 / 127));
    let pan = 0;
    if (Math.abs(slot.pan0to127 - 64) > 1) {
      pan = Math.max(-1, Math.min(1, (slot.pan0to127 - 63.5) / 63.5));
    }

    if (slot.wired && slot.gainNode) {
      slot.gainNode.gain.value = vol;
      if (slot.pannerNode) {
        slot.pannerNode.pan.value = pan;
      }
    } else {
      slot.audio.volume = vol;
    }
  }

  function wireWebAudioNodes(slot) {
    if (slot.wired || !audioCtx) return;
    try {
      const src = audioCtx.createMediaElementSource(slot.audio);
      const gain = audioCtx.createGain();
      const panner =
        typeof audioCtx.createStereoPanner === 'function' ? audioCtx.createStereoPanner() : null;

      if (panner) {
        src.connect(panner);
        panner.connect(gain);
      } else {
        src.connect(gain);
      }
      gain.connect(audioCtx.destination);

      slot.sourceNode = src;
      slot.gainNode = gain;
      slot.pannerNode = panner;
      slot.wired = true;

      slot.audio.volume = 1.0;
      applySlotGainAndPan(slot);
    } catch (err) {
      console.warn('[GtaAudioStream] WebAudio node wiring fallback:', err);
    }
  }

  function attemptPlay(slot) {
    if (slot.stopped || slot.paused || !slot.url) return;
    if (audioCtx && audioCtx.state === 'suspended' && audioUnlocked) {
      audioCtx.resume().catch(() => {});
    }
    try {
      const playPromise = slot.audio.play();
      if (playPromise && typeof playPromise.then === 'function') {
        playPromise
          .then(() => {
            slot.pendingPlay = false;
          })
          .catch(() => {
            if (!slot.stopped && !slot.paused) {
              slot.pendingPlay = true;
            }
          });
      }
    } catch (_) {
      if (!slot.stopped && !slot.paused) {
        slot.pendingPlay = true;
      }
    }
  }

  function onFirstUserGesture() {
    audioUnlocked = true;
    const ctx = ensureAudioContext();
    if (ctx && ctx.state === 'suspended') {
      ctx.resume().catch(() => {});
    }
    for (const slot of streamSlots.values()) {
      wireWebAudioNodes(slot);
      if (slot.pendingPlay && !slot.stopped && !slot.paused && !slot.ended) {
        attemptPlay(slot);
      }
    }
  }

  function attachGestureUnlock() {
    if (unlockListenersAttached || typeof window === 'undefined') return;
    unlockListenersAttached = true;
    const events = ['pointerdown', 'keydown', 'mousedown', 'touchstart', 'click'];
    const handler = () => {
      onFirstUserGesture();
      if (audioCtx && audioCtx.state === 'running') {
        for (const ev of events) {
          window.removeEventListener(ev, handler, true);
        }
      }
    };
    for (const ev of events) {
      window.addEventListener(ev, handler, { capture: true, passive: true });
    }
  }

  function getOrCreateSlot(streamIdx) {
    const idx = Number(streamIdx) | 0;
    let slot = streamSlots.get(idx);
    if (slot) return slot;

    const audio = new Audio();
    audio.preload = 'auto';
    audio.crossOrigin = 'anonymous';

    slot = {
      idx,
      audio,
      url: null,
      filename: '',
      segments: null,
      segIdx: 0,
      preloadedUrl: null,
      preloadedFilename: '',
      preloadedSegments: null,
      stopped: true,
      paused: false,
      ended: false,
      pendingPlay: false,
      pendingSeekSec: null,
      vol0to127: 127,
      pan0to127: 64,
      wired: false,
      sourceNode: null,
      gainNode: null,
      pannerNode: null,
    };

    audio.addEventListener('loadedmetadata', () => {
      if (slot.pendingSeekSec !== null) {
        const dur = Number.isFinite(audio.duration) ? audio.duration : slot.pendingSeekSec;
        const target = Math.max(0, Math.min(slot.pendingSeekSec, Math.max(0, dur - 0.05)));
        try {
          audio.currentTime = target;
        } catch (_) {
          // Ignore seek errors on unready streams
        }
        slot.pendingSeekSec = null;
      }
    });

    audio.addEventListener('ended', () => {
      if (slot.segments && slot.segIdx + 1 < slot.segments.length && !slot.stopped) {
        slot.segIdx += 1;
        const nextSeg = slot.segments[slot.segIdx];
        slot.url = nextSeg.url;
        slot.ended = false;
        slot.pendingSeekSec = 0;
        slot.audio.src = nextSeg.url;
        try {
          slot.audio.load();
        } catch (_) {}
        applySlotGainAndPan(slot);
        attemptPlay(slot);
        return;
      }
      slot.ended = true;
      slot.pendingPlay = false;
    });

    audio.addEventListener('error', () => {
      slot.ended = true;
      slot.pendingPlay = false;
      console.warn(`[GtaAudioStream] Audio error on stream ${idx}: ${slot.url}`);
    });

    if (audioUnlocked) {
      ensureAudioContext();
      wireWebAudioNodes(slot);
    }

    streamSlots.set(idx, slot);
    return slot;
  }

  attachGestureUnlock();

  const GtaAudioStream = {
    unlock() {
      onFirstUserGesture();
    },

    preload(streamIdx, filename) {
      const slot = getOrCreateSlot(streamIdx);
      const track = resolveAudioTrack(filename);
      if (!track || !track.url) return false;

      if (!cachedManifest) {
        fetchManifest(false).catch(() => {});
      }

      slot.preloadedUrl = track.url;
      slot.preloadedSegments = track.segments;
      slot.preloadedFilename = String(filename || '');
      slot.segments = track.segments;
      slot.segIdx = 0;
      slot.stopped = true;
      slot.paused = false;
      slot.ended = false;
      slot.pendingPlay = false;
      slot.pendingSeekSec = null;

      try {
        slot.audio.pause();
      } catch (_) {}

      if (slot.url !== track.url) {
        slot.url = track.url;
        slot.filename = slot.preloadedFilename;
        slot.audio.src = track.url;
        try {
          slot.audio.load();
        } catch (_) {}
      } else if (slot.audio.readyState >= 1) {
        try {
          slot.audio.currentTime = 0;
        } catch (_) {}
      }
      return true;
    },

    startPreloaded(streamIdx) {
      const slot = getOrCreateSlot(streamIdx);
      const url = slot.preloadedUrl || slot.url;
      if (!url) return false;

      if (slot.preloadedSegments) {
        slot.segments = slot.preloadedSegments;
        slot.segIdx = 0;
      }

      if (slot.url !== url) {
        slot.url = url;
        slot.filename = slot.preloadedFilename || '';
        slot.audio.src = url;
      }
      slot.stopped = false;
      slot.paused = false;
      slot.ended = false;
      slot.pendingSeekSec = null;

      if (slot.audio.readyState >= 1 && slot.audio.currentTime > 0.05) {
        try {
          slot.audio.currentTime = 0;
        } catch (_) {}
      }
      applySlotGainAndPan(slot);
      attemptPlay(slot);
      return true;
    },

    start(streamIdx, filename, posMs = 0) {
      const slot = getOrCreateSlot(streamIdx);
      const track = resolveAudioTrack(filename);
      if (!track || !track.url) return false;

      if (!cachedManifest) {
        fetchManifest(false).catch(() => {});
      }

      const rawPosMs = Math.max(0, Number(posMs) || 0);
      let targetUrl = track.url;
      let seekSec = rawPosMs / 1000;

      if (track.segments && track.segments.length > 0) {
        const segIdx = findSegmentIndex(track.segments, rawPosMs);
        const seg = track.segments[segIdx];
        slot.segments = track.segments;
        slot.segIdx = segIdx;
        targetUrl = seg.url;
        seekSec = Math.max(0, (rawPosMs - (seg.startMs || 0)) / 1000);
      } else {
        slot.segments = null;
        slot.segIdx = 0;
      }

      slot.stopped = false;
      slot.paused = false;
      slot.ended = false;
      slot.pendingPlay = false;

      if (slot.url !== targetUrl) {
        slot.url = targetUrl;
        slot.filename = String(filename || '');
        slot.pendingSeekSec = seekSec > 0 ? seekSec : null;
        slot.audio.src = targetUrl;
        try {
          slot.audio.load();
        } catch (_) {}
      } else {
        if (slot.audio.readyState >= 1) {
          slot.pendingSeekSec = null;
          try {
            slot.audio.currentTime = seekSec;
          } catch (_) {
            slot.pendingSeekSec = seekSec > 0 ? seekSec : null;
          }
        } else {
          slot.pendingSeekSec = seekSec > 0 ? seekSec : null;
        }
      }

      applySlotGainAndPan(slot);
      attemptPlay(slot);
      return true;
    },

    stop(streamIdx) {
      const slot = getOrCreateSlot(streamIdx);
      slot.stopped = true;
      slot.paused = false;
      slot.ended = false;
      slot.pendingPlay = false;
      slot.pendingSeekSec = null;
      slot.segIdx = 0;
      try {
        slot.audio.pause();
        if (slot.audio.readyState >= 1) {
          slot.audio.currentTime = 0;
        }
      } catch (_) {}
    },

    pause(streamIdx, paused) {
      const slot = getOrCreateSlot(streamIdx);
      const shouldPause = Boolean(paused);
      slot.paused = shouldPause;
      if (shouldPause) {
        slot.pendingPlay = false;
        try {
          slot.audio.pause();
        } catch (_) {}
      } else if (!slot.stopped && !slot.ended && slot.url) {
        attemptPlay(slot);
      }
    },

    setVolumeAndPan(streamIdx, vol0to127, pan0to127 = 64) {
      const slot = getOrCreateSlot(streamIdx);
      const v = Number(vol0to127);
      const p = Number(pan0to127);
      slot.vol0to127 = Number.isFinite(v) ? Math.max(0, Math.min(127, v)) : 127;
      slot.pan0to127 = Number.isFinite(p) ? Math.max(0, Math.min(127, p)) : 64;
      applySlotGainAndPan(slot);
    },

    getPositionMs(streamIdx) {
      const slot = streamSlots.get(Number(streamIdx) | 0);
      if (!slot || slot.stopped || !slot.url) return 0;
      const segStartMs =
        slot.segments && slot.segments[slot.segIdx] ? slot.segments[slot.segIdx].startMs || 0 : 0;
      if (slot.pendingSeekSec !== null) {
        return (segStartMs + slot.pendingSeekSec * 1000) | 0;
      }
      const t = slot.audio.currentTime;
      return Number.isFinite(t) ? (segStartMs + t * 1000) | 0 : segStartMs | 0;
    },

    isPlaying(streamIdx) {
      const slot = streamSlots.get(Number(streamIdx) | 0);
      if (!slot || !slot.url) return false;
      return !slot.audio.ended && !slot.ended && !slot.paused && !slot.stopped;
    },

    getDebugState() {
      const out = {};
      for (const [idx, slot] of streamSlots.entries()) {
        out[idx] = {
          filename: slot.filename,
          url: slot.url,
          segIdx: slot.segIdx,
          segmentCount: slot.segments ? slot.segments.length : 1,
          playing: this.isPlaying(idx),
          positionMs: this.getPositionMs(idx),
          vol: slot.vol0to127,
          pan: slot.pan0to127,
          webAudio: slot.wired,
        };
      }
      return out;
    },
  };

  if (typeof window !== 'undefined') {
    window.GtaAssetLoader = GtaAssetLoader;
    window.GtaAudioStream = GtaAudioStream;
  }
})();
