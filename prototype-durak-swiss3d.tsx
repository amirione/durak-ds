import React, { useState, useRef, useEffect, useCallback, useMemo } from "react";
import * as THREE from "three";

/* ---------------------------------------------------------
   ДУРАК DS — Swiss minimalism, 3D cards, dual-screen layout
   Plain Three.js (no react-three/fiber) for artifact compat.
   Обычные правила: 6 карт, отбой старшей картой той же масти
   или козырем, взял — добор до 6.
--------------------------------------------------------- */

const SUITS = ["spades", "hearts", "diamonds", "clubs"];
const RANKS = ["6", "7", "8", "9", "10", "J", "Q", "K", "A"];
const RANK_VALUE = Object.fromEntries(RANKS.map((r, i) => [r, i]));
const RED_SUITS = new Set(["hearts", "diamonds"]);
const ACCENT = "#C1121F";
const INK = "#0A0A0A";
const PAPER = "#FFFFFF";
const SHELL = "#E8E6E0";
const SHELL_DARK = "#D3D0C8";
const SUIT_LABEL = { spades: "пики", hearts: "черви", diamonds: "бубны", clubs: "трефы" };

function makeDeck() {
  const deck = [];
  for (const s of SUITS) for (const r of RANKS) deck.push({ suit: s, rank: r, id: `${r}-${s}` });
  return deck;
}
function shuffle(arr) {
  const a = [...arr];
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [a[i], a[j]] = [a[j], a[i]];
  }
  return a;
}
function beats(attacker, defender, trump) {
  if (defender.suit === attacker.suit) return RANK_VALUE[defender.rank] > RANK_VALUE[attacker.rank];
  if (defender.suit === trump && attacker.suit !== trump) return true;
  return false;
}

/* ---------------- Card texture (canvas -> texture, Swiss grotesk) ---------------- */
const textureCache = new Map();
function buildCardTexture(card, { isTrump = false, highlight = false } = {}) {
  const key = `${card.id}-${isTrump}-${highlight}`;
  if (textureCache.has(key)) return textureCache.get(key);

  const W = 360, H = 500;
  const cv = document.createElement("canvas");
  cv.width = W; cv.height = H;
  const ctx = cv.getContext("2d");

  // background
  ctx.fillStyle = PAPER;
  ctx.fillRect(0, 0, W, H);
  // border
  ctx.strokeStyle = highlight ? ACCENT : INK;
  ctx.lineWidth = highlight ? 6 : 3;
  ctx.strokeRect(ctx.lineWidth / 2, ctx.lineWidth / 2, W - ctx.lineWidth, H - ctx.lineWidth);

  const color = RED_SUITS.has(card.suit) ? ACCENT : INK;
  ctx.fillStyle = color;

  // rank top-left
  ctx.font = "700 56px 'Helvetica Neue', Arial, sans-serif";
  ctx.textAlign = "left";
  ctx.textBaseline = "top";
  ctx.fillText(card.rank, 22, 18);

  // rank bottom-right (mirrored)
  ctx.save();
  ctx.translate(W - 22, H - 18);
  ctx.rotate(Math.PI);
  ctx.textAlign = "left";
  ctx.textBaseline = "top";
  ctx.fillText(card.rank, 0, 0);
  ctx.restore();

  // suit glyph, center
  drawSuitGlyph(ctx, card.suit, W / 2, H / 2, 92, color);

  if (isTrump) {
    ctx.fillStyle = ACCENT;
    ctx.fillRect(W / 2 - 70, H - 34, 140, 5);
  }

  const tex = new THREE.CanvasTexture(cv);
  tex.anisotropy = 8;
  tex.needsUpdate = true;
  textureCache.set(key, tex);
  return tex;
}

function drawSuitGlyph(ctx, suit, cx, cy, size, color) {
  ctx.fillStyle = color;
  const s = size / 100;
  ctx.save();
  ctx.translate(cx, cy);
  switch (suit) {
    case "spades": {
      ctx.beginPath();
      ctx.moveTo(0, -50 * s);
      ctx.lineTo(40 * s, 20 * s);
      ctx.quadraticCurveTo(40 * s, 45 * s, 10 * s, 40 * s);
      ctx.lineTo(0, 30 * s);
      ctx.lineTo(-10 * s, 40 * s);
      ctx.quadraticCurveTo(-40 * s, 45 * s, -40 * s, 20 * s);
      ctx.closePath();
      ctx.fill();
      ctx.fillRect(-6 * s, 30 * s, 12 * s, 32 * s);
      break;
    }
    case "clubs": {
      [[0, -30], [-26, 12], [26, 12]].forEach(([x, y]) => {
        ctx.beginPath();
        ctx.arc(x * s, y * s, 26 * s, 0, Math.PI * 2);
        ctx.fill();
      });
      ctx.fillRect(-6 * s, 20 * s, 12 * s, 42 * s);
      break;
    }
    case "diamonds": {
      ctx.beginPath();
      ctx.moveTo(0, -50 * s);
      ctx.lineTo(35 * s, 0);
      ctx.lineTo(0, 50 * s);
      ctx.lineTo(-35 * s, 0);
      ctx.closePath();
      ctx.fill();
      break;
    }
    case "hearts":
    default: {
      ctx.beginPath();
      ctx.moveTo(0, 44 * s);
      ctx.bezierCurveTo(-55 * s, -10 * s, -30 * s, -50 * s, 0, -18 * s);
      ctx.bezierCurveTo(30 * s, -50 * s, 55 * s, -10 * s, 0, 44 * s);
      ctx.closePath();
      ctx.fill();
      break;
    }
  }
  ctx.restore();
}

function backTexture() {
  const key = "back";
  if (textureCache.has(key)) return textureCache.get(key);
  const W = 360, H = 500;
  const cv = document.createElement("canvas");
  cv.width = W; cv.height = H;
  const ctx = cv.getContext("2d");
  ctx.fillStyle = INK;
  ctx.fillRect(0, 0, W, H);
  ctx.strokeStyle = SHELL;
  ctx.lineWidth = 3;
  ctx.strokeRect(1.5, 1.5, W - 3, H - 3);
  ctx.strokeStyle = PAPER;
  ctx.lineWidth = 2;
  ctx.strokeRect(40, 40, W - 80, H - 80);
  const tex = new THREE.CanvasTexture(cv);
  textureCache.set(key, tex);
  return tex;
}

const CARD_W = 0.72, CARD_H = 1.0;
const cardGeo = new THREE.PlaneGeometry(CARD_W, CARD_H);

/* ---------------- One DS "screen" — its own renderer/scene/camera ---------------- */
function useThreeScreen(mountRef, { interactive = false, onPick, bgColor = PAPER } = {}) {
  const stateRef = useRef({
    renderer: null,
    scene: null,
    camera: null,
    cards: new Map(), // id -> {mesh, baseY, baseZ, baseRotX, hovered}
    raycaster: new THREE.Raycaster(),
    mouse: new THREE.Vector2(),
    animId: null,
  });
  const interactiveRef = useRef(interactive);
  const onPickRef = useRef(onPick);
  interactiveRef.current = interactive;
  onPickRef.current = onPick;

  useEffect(() => {
    const mount = mountRef.current;
    if (!mount) return;
    const width = 340, height = 255;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(bgColor);

    const aspect = width / height;
    const viewSize = 2.6;
    const camera = new THREE.OrthographicCamera(
      (-viewSize * aspect) / 2, (viewSize * aspect) / 2,
      viewSize / 2, -viewSize / 2,
      0.1, 100
    );
    camera.position.set(0, 0, 5);
    camera.lookAt(0, 0, 0);

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setSize(width, height);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    mount.innerHTML = "";
    mount.appendChild(renderer.domElement);

    const ambient = new THREE.AmbientLight(0xffffff, 0.75);
    scene.add(ambient);
    const dir = new THREE.DirectionalLight(0xffffff, 0.85);
    dir.position.set(1.6, 3.2, 2.4);
    dir.castShadow = true;
    dir.shadow.mapSize.set(512, 512);
    dir.shadow.camera.left = -2;
    dir.shadow.camera.right = 2;
    dir.shadow.camera.top = 2;
    dir.shadow.camera.bottom = -2;
    dir.shadow.camera.near = 0.5;
    dir.shadow.camera.far = 8;
    dir.shadow.bias = -0.0015;
    scene.add(dir);

    // table surface — catches shadows, gives the screen depth
    const tableGeo = new THREE.PlaneGeometry(6, 6);
    const tableMat = new THREE.ShadowMaterial({ opacity: 0.16 });
    const tableMesh = new THREE.Mesh(tableGeo, tableMat);
    tableMesh.position.z = -1.3;
    tableMesh.receiveShadow = true;
    scene.add(tableMesh);

    const s = stateRef.current;
    s.renderer = renderer;
    s.scene = scene;
    s.camera = camera;

    function animate() {
      s.animId = requestAnimationFrame(animate);
      s.cards.forEach((c) => {
        const wantUp = c.hovered;
        const liftY = wantUp ? 0.22 : 0;
        const liftZ = wantUp ? 0.5 : 0;
        const liftScale = wantUp ? 1.1 : 1.0;
        const liftRotX = wantUp ? -0.15 : 0;

        const goalX = c.targetX ?? c.mesh.position.x;
        const goalY = (c.targetY ?? c.baseY) + liftY;
        const goalZ = (c.targetZ ?? c.baseZ) + liftZ;
        const goalScale = (c.targetScale ?? 1) * liftScale;
        const goalRotX = (c.targetRotX ?? c.baseRotX) + liftRotX;
        const goalRotZ = c.targetRotZ ?? c.mesh.rotation.z;

        const ease = 0.22;
        c.mesh.position.x += (goalX - c.mesh.position.x) * ease;
        c.mesh.position.y += (goalY - c.mesh.position.y) * ease;
        c.mesh.position.z += (goalZ - c.mesh.position.z) * ease;
        c.mesh.scale.x += (goalScale - c.mesh.scale.x) * ease;
        c.mesh.scale.y += (goalScale - c.mesh.scale.y) * ease;
        c.mesh.rotation.x += (goalRotX - c.mesh.rotation.x) * ease;
        c.mesh.rotation.z += (goalRotZ - c.mesh.rotation.z) * ease;
      });
      renderer.render(scene, camera);
    }
    animate();

    function toLocalCoords(clientX, clientY) {
      const rect = renderer.domElement.getBoundingClientRect();
      const x = ((clientX - rect.left) / rect.width) * 2 - 1;
      const y = -((clientY - rect.top) / rect.height) * 2 + 1;
      return { x, y };
    }

    function pick(clientX, clientY) {
      const { x, y } = toLocalCoords(clientX, clientY);
      s.mouse.set(x, y);
      s.raycaster.setFromCamera(s.mouse, camera);
      const meshes = Array.from(s.cards.values()).map((c) => c.mesh);
      const hits = s.raycaster.intersectObjects(meshes, false);
      return hits.length ? hits[0].object : null;
    }

    function handleMove(e) {
      if (!interactiveRef.current) return;
      const point = e.touches ? e.touches[0] : e;
      const hit = pick(point.clientX, point.clientY);
      s.cards.forEach((c) => { c.hovered = c.mesh === hit; });
      renderer.domElement.style.cursor = hit ? "pointer" : "default";
    }
    function handleClick(e) {
      if (!interactiveRef.current) return;
      const point = e.changedTouches ? e.changedTouches[0] : e;
      const hit = pick(point.clientX, point.clientY);
      if (hit && onPickRef.current) onPickRef.current(hit.userData.cardId);
    }

    renderer.domElement.addEventListener("mousemove", handleMove);
    renderer.domElement.addEventListener("click", handleClick);
    renderer.domElement.addEventListener("touchmove", handleMove, { passive: true });
    renderer.domElement.addEventListener("touchend", handleClick);

    return () => {
      cancelAnimationFrame(s.animId);
      renderer.domElement.removeEventListener("mousemove", handleMove);
      renderer.domElement.removeEventListener("click", handleClick);
      renderer.domElement.removeEventListener("touchmove", handleMove);
      renderer.domElement.removeEventListener("touchend", handleClick);
      renderer.dispose();
      if (mount) mount.innerHTML = "";
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return stateRef;
}

function clearGroup(scene, cardsMap) {
  cardsMap.forEach((c) => {
    scene.remove(c.mesh);
    c.mesh.geometry?.dispose?.();
  });
  cardsMap.clear();
}

// Upserts a card into the scene: reuses the mesh if the card already exists
// (animating smoothly to its new target position), or creates a fresh mesh
// that animates in from a spawn point (e.g. the deck).
function upsertCardMesh(scene, cardsMap, card, opts) {
  const { x, y, z, rotX = 0, rotZ = 0, faceUp = true, isTrump = false, highlight = false, scale = 1, spawnFrom = null } = opts;
  let entry = cardsMap.get(card.id);

  if (entry) {
    // update texture in case face-up/trump/highlight state changed
    const tex = faceUp ? buildCardTexture(card, { isTrump, highlight }) : backTexture();
    entry.mesh.material.map = tex;
    entry.mesh.material.needsUpdate = true;
    entry.targetX = x; entry.targetY = y; entry.targetZ = z;
    entry.targetRotX = rotX; entry.targetRotZ = rotZ; entry.targetScale = scale;
    entry.baseY = y; entry.baseZ = z; entry.baseRotX = rotX;
    entry.alive = true;
    return entry.mesh;
  }

  const tex = faceUp ? buildCardTexture(card, { isTrump, highlight }) : backTexture();
  const mat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.9 });
  const mesh = new THREE.Mesh(cardGeo, mat);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  const startX = spawnFrom ? spawnFrom.x : x;
  const startY = spawnFrom ? spawnFrom.y : y;
  const startZ = spawnFrom ? spawnFrom.z : z;
  mesh.position.set(startX, startY, startZ);
  mesh.rotation.set(rotX, 0, rotZ);
  mesh.scale.setScalar(spawnFrom ? scale * 0.7 : scale);
  mesh.userData.cardId = card.id;
  scene.add(mesh);
  cardsMap.set(card.id, {
    mesh,
    baseY: y, baseZ: z, baseRotX: rotX,
    targetX: x, targetY: y, targetZ: z,
    targetRotX: rotX, targetRotZ: rotZ, targetScale: scale,
    hovered: false, alive: true,
  });
  return mesh;
}

// Marks entries not present in this render pass, then removes them next frame
// (so a card that's leaving can still be seen; for now we remove immediately
// but keep the structure ready for exit animation).
function pruneStale(scene, cardsMap, keepIds) {
  const toRemove = [];
  cardsMap.forEach((entry, id) => {
    if (!keepIds.has(id)) toRemove.push(id);
  });
  toRemove.forEach((id) => {
    const entry = cardsMap.get(id);
    scene.remove(entry.mesh);
    entry.mesh.geometry?.dispose?.();
    cardsMap.delete(id);
  });
}

// legacy alias used by a couple of call sites below
function addCardMesh(scene, cardsMap, card, opts) {
  return upsertCardMesh(scene, cardsMap, card, opts);
}

/* ---------------- Table screen ---------------- */
function TableScreen({ trumpCard, deckCount, table }) {
  const mountRef = useRef();
  const screenState = useThreeScreen(mountRef, { interactive: false, bgColor: "#F5F4F0" });

  useEffect(() => {
    const s = screenState.current;
    if (!s.scene) return;
    const keepIds = new Set();

    // table pairs — dynamic scale: fewer cards = bigger & closer, more cards = compact
    // deck/trump are shown as a corner HTML icon now, so the full table is free for battle cards
    const n = table.length;
    const perRow = n <= 2 ? 2 : n <= 6 ? 3 : 4;
    const rows = Math.ceil(n / perRow) || 1;
    let cardScale = n === 0 ? 0.6
      : n <= 2 ? 1.05
      : n <= 4 ? 0.88
      : n <= 6 ? 0.7
      : 0.56;

    // Clamp step so the widest row (perRow cards + defend-card offset) fits
    // within the visible viewport (~3.3 wide at this camera's aspect), with
    // a small margin — prevents cards from spilling off the screen edge.
    const viewportWidth = 3.1; // slightly under the true ~3.3 visible width, for a safety margin
    const maxRowSpan = viewportWidth - cardScale * (0.72 + 0.4); // card width + defend-card offset margin
    let stepX = cardScale * 1.55;
    if (perRow > 1) {
      const neededSpan = stepX * (perRow - 1);
      if (neededSpan > maxRowSpan) {
        stepX = maxRowSpan / (perRow - 1);
        // shrink cards too if even the minimum step is still too tight
        const minStepForScale = cardScale * 0.85;
        if (stepX < minStepForScale) {
          cardScale = cardScale * (stepX / minStepForScale);
        }
      }
    }
    const stepY = cardScale * 1.5;
    table.forEach((pair, i) => {
      const col = i % perRow, row = Math.floor(i / perRow);
      const rowCount = Math.min(n - row * perRow, perRow);
      const x = (col - (rowCount - 1) / 2) * stepX;
      const y = (rows - 1) * stepY * 0.5 - row * stepY;
      const jitter = ((i * 37) % 7) / 100 - 0.03; // deterministic subtle variance
      keepIds.add(pair.attack.id);
      upsertCardMesh(s.scene, s.cards, pair.attack, {
        x, y, z: 0, rotX: 0.12, rotZ: jitter, scale: cardScale, spawnFrom: { x: 0, y: -1.4, z: 0.4 },
      });
      if (pair.defend) {
        keepIds.add(pair.defend.id);
        upsertCardMesh(s.scene, s.cards, pair.defend, {
          x: x + cardScale * 0.2, y: y - cardScale * 0.13, z: 0.04, rotX: 0.12, rotZ: jitter + 0.32,
          scale: cardScale, spawnFrom: { x: 0, y: -1.4, z: 0.4 },
        });
      }
    });

    pruneStale(s.scene, s.cards, keepIds);
  }, [table, screenState]);

  return (
    <div style={{ position: "relative", width: 340, height: 255 }}>
      <div ref={mountRef} style={{ width: 340, height: 255 }} />
      <TrumpBadge trumpCard={trumpCard} deckCount={deckCount} />
    </div>
  );
}

function TrumpBadge({ trumpCard, deckCount }) {
  if (!trumpCard) return null;
  const color = RED_SUITS.has(trumpCard.suit) ? ACCENT : INK;
  return (
    <div style={{
      position: "absolute", bottom: 8, right: 10, display: "flex", alignItems: "center", gap: 6,
      fontFamily: "'Helvetica Neue', Arial, sans-serif", pointerEvents: "none",
    }}>
      {deckCount > 0 && <div style={{ fontSize: 10, letterSpacing: "0.04em", color: INK, opacity: 0.55 }}>{deckCount}</div>}
      <div style={{
        width: 26, height: 34, background: PAPER, border: `1.5px solid ${INK}`,
        display: "flex", alignItems: "center", justifyContent: "center", position: "relative",
      }}>
        {deckCount > 0 && <div style={{ position: "absolute", top: 2, left: 3, fontSize: 9, fontWeight: 700, color }}>{trumpCard.rank}</div>}
        <SuitIcon suit={trumpCard.suit} color={color} size={11} />
      </div>
    </div>
  );
}

function SuitIcon({ suit, color, size = 12 }) {
  const s = size;
  const common = { width: s, height: s, display: "block" };
  switch (suit) {
    case "diamonds":
      return <div style={{ ...common, background: color, transform: "rotate(45deg)", width: s * 0.75, height: s * 0.75 }} />;
    case "hearts":
      return (
        <svg width={s} height={s} viewBox="0 0 24 24" style={{ display: "block" }}>
          <path d="M12 21s-8-5.2-8-11.2C4 6 6.5 4 9 4c1.5 0 3 1 3 3 0-2 1.5-3 3-3 2.5 0 5 2 5 5.8C20 15.8 12 21 12 21z" fill={color} />
        </svg>
      );
    case "clubs":
      return (
        <svg width={s} height={s} viewBox="0 0 24 24" style={{ display: "block" }}>
          <circle cx="12" cy="7" r="4.2" fill={color} />
          <circle cx="7" cy="14" r="4.2" fill={color} />
          <circle cx="17" cy="14" r="4.2" fill={color} />
          <rect x="10.5" y="14" width="3" height="8" fill={color} />
        </svg>
      );
    case "spades":
    default:
      return (
        <svg width={s} height={s} viewBox="0 0 24 24" style={{ display: "block" }}>
          <path d="M12 2C9 7 3 11 3 15.5c0 3 2.3 4.5 4.8 4.2C9 19.5 10 18.7 10.5 17.5c-0.3 2-1.8 3.2-3.5 3.6V22h10v-0.9c-1.7-.4-3.2-1.6-3.5-3.6.5 1.2 1.5 2 2.7 2.2 2.5.3 4.8-1.2 4.8-4.2C21 11 15 7 12 2z" fill={color} />
        </svg>
      );
  }
}

/* ---------------- Hand screen (interactive) ---------------- */
function HandScreen({ hand, onSelect, selectable }) {
  const mountRef = useRef();
  const screenState = useThreeScreen(mountRef, {
    interactive: selectable,
    onPick: (cardId) => { if (onSelect) onSelect(cardId); },
  });

  useEffect(() => {
    const s = screenState.current;
    if (!s.scene) return;
    const keepIds = new Set();
    const n = hand.length;
    const spread = Math.min(0.5, 3.2 / Math.max(n, 1));
    hand.forEach((card, i) => {
      const x = (i - (n - 1) / 2) * spread;
      const rotZ = (i - (n - 1) / 2) * -0.04;
      keepIds.add(card.id);
      upsertCardMesh(s.scene, s.cards, card, {
        x, y: -0.25, z: i * 0.01, rotX: 0.1, rotZ, scale: 0.95,
        spawnFrom: { x: -1.1, y: -0.9, z: -0.5 },
      });
    });
    pruneStale(s.scene, s.cards, keepIds);
  }, [hand, screenState]);

  return <div ref={mountRef} style={{ width: 340, height: 255 }} />;
}

/* ---------------- DS screen frame ---------------- */
function DSFrame({ label, children }) {
  return (
    <div style={{ position: "relative" }}>
      <div style={{ width: 340, height: 255, border: `1px solid ${INK}`, overflow: "hidden" }}>
        {children}
      </div>
      <div
        style={{
          position: "absolute", top: 8, left: 10,
          fontFamily: "'Helvetica Neue', Arial, sans-serif",
          fontSize: 10, letterSpacing: "0.06em", color: PAPER,
          mixBlendMode: "difference", pointerEvents: "none",
        }}
      >
        {label}
      </div>
    </div>
  );
}

/* ---------------- Game logic (N players: you + 1-3 bots, circular turn order) ---------------- */
// player[0] is always the human. players[1..] are bots.
function useDurakGame(mode, botCount, cheating) {
  const [state, setState] = useState(() => init(mode, botCount, cheating));

  function init(m, nBots, cheat) {
    const deck = shuffle(makeDeck());
    const nPlayers = 1 + nBots;
    const hands = [];
    for (let i = 0; i < nPlayers; i++) hands.push(deck.splice(0, 6));
    const trumpCard = deck[deck.length - 1];
    const trump = trumpCard.suit;
    return {
      deck, trumpCard, trump, mode: m || "classic", cheating: !!cheat,
      hands, // hands[0] = human, hands[1..] = bots
      table: [], // { attack, defend, attackerIdx }
      attackerIdx: 0,
      defenderIdx: nPlayers > 1 ? 1 : 0,
      activeIdx: 0, // whose action we're waiting on right now
      phase: "attack", // attack | defend
      message: "Твой ход — выбери карту для атаки",
      gameOver: null, // null | "draw" | playerIdx of the loser
      out: [], // indices of players who've gone out (emptied hand, deck empty)
    };
  }

  const draw = useCallback((hand, deck, need) => {
    const newHand = [...hand];
    const newDeck = [...deck];
    while (newHand.length < need && newDeck.length) newHand.push(newDeck.shift());
    return [newHand, newDeck];
  }, []);

  // Refill order: attacker first, then around the table, defender last.
  const refill = useCallback((s) => {
    const n = s.hands.length;
    const order = [];
    for (let i = 0; i < n; i++) order.push((s.attackerIdx + i) % n);
    let deck = s.deck;
    const hands = [...s.hands];
    for (const idx of order) {
      const [newHand, newDeck] = draw(hands[idx], deck, 6);
      hands[idx] = newHand;
      deck = newDeck;
    }
    return { ...s, hands, deck };
  }, [draw]);

  const openRanksOf = (table) => new Set(table.flatMap((p) => [p.attack.rank, p.defend?.rank].filter(Boolean)));

  // Finds the next player around the circle who still has cards (or cards
  // can still be drawn for them). Players who are out (empty hand, empty
  // deck) are skipped so the turn order doesn't stall on them.
  const nextActive = (hands, deck, fromIdx) => {
    const n = hands.length;
    for (let step = 1; step <= n; step++) {
      const idx = (fromIdx + step) % n;
      if (hands[idx].length > 0 || deck.length > 0) return idx;
    }
    return fromIdx; // everyone out — shouldn't happen, game-over effect handles this
  };

  // Any player (attacker or, in podkidnoy, anyone) throws in a card matching an open rank.
  const throwIn = useCallback((playerIdx, card) => {
    setState((s) => {
      if (s.phase !== "defend") return s;
      if (s.table.length >= 6) return s;
      if (playerIdx === s.defenderIdx) return s; // defender can't throw in on themself
      if (playerIdx !== s.attackerIdx && s.mode !== "podkidnoy") return s;
      if (!s.cheating) {
        const openRanks = openRanksOf(s.table);
        if (s.table.length > 0 && !openRanks.has(card.rank)) return s;
      }
      const hand = s.hands[playerIdx].filter((c) => c.id !== card.id);
      const hands = s.hands.map((h, i) => (i === playerIdx ? hand : h));
      const table = [...s.table, { attack: card, defend: null, attackerIdx: playerIdx }];
      const msg = playerIdx === 0 ? "Подкинул. Ждём защиту…" : "Подкинули карту.";
      return { ...s, hands, table, message: msg };
    });
  }, []);

  const playerAttack = useCallback((card) => {
    setState((s) => {
      if (s.phase !== "attack" || s.activeIdx !== 0) return s;
      const openRanks = openRanksOf(s.table);
      if (s.table.length > 0 && !openRanks.has(card.rank)) return s;
      if (s.table.length >= 6) return s;
      const hand = s.hands[0].filter((c) => c.id !== card.id);
      const hands = s.hands.map((h, i) => (i === 0 ? hand : h));
      const table = [...s.table, { attack: card, defend: null, attackerIdx: 0 }];
      return { ...s, hands, table, phase: "defend", activeIdx: s.defenderIdx, message: "Ждём защиту…" };
    });
  }, []);

  const playerThrowIn = useCallback((card) => throwIn(0, card), [throwIn]);

  // Perevodnoy: the defender, instead of beating the first (undefended) card,
  // may "transfer" it (and any others of the same rank they're holding) to the
  // next player around the circle — only if nothing on the table has been
  // defended yet, and only in perevodnoy mode.
  const playerTransfer = useCallback((card) => {
    setState((s) => {
      if (s.mode !== "perevodnoy") return s;
      if (s.phase !== "defend" || s.activeIdx !== 0) return s;
      const openPair = s.table.find((p) => !p.defend);
      if (!openPair) return s;
      const anyDefended = s.table.some((p) => p.defend);
      if (anyDefended) return s;
      if (card.rank !== openPair.attack.rank) return s;
      const newDefenderIdx = nextActive(s.hands, s.deck, s.defenderIdx);
      const hand = s.hands[0].filter((c) => c.id !== card.id);
      const hands = s.hands.map((h, i) => (i === 0 ? hand : h));
      const table = [...s.table, { attack: card, defend: null, attackerIdx: 0 }];
      return {
        ...s, hands, table,
        defenderIdx: newDefenderIdx, activeIdx: newDefenderIdx,
        message: newDefenderIdx === 0 ? "Перевод тебе — отбивайся!" : "Перевёл дальше.",
      };
    });
  }, []);

  const playerDefend = useCallback((card) => {
    setState((s) => {
      if (s.phase !== "defend" || s.activeIdx !== 0 || s.defenderIdx !== 0) return s;
      const openPair = s.table.find((p) => !p.defend);
      if (!openPair) return s;
      if (!beats(openPair.attack, card, s.trump)) return s;
      const hand = s.hands[0].filter((c) => c.id !== card.id);
      const hands = s.hands.map((h, i) => (i === 0 ? hand : h));
      const table = s.table.map((p) => (p === openPair ? { ...p, defend: card } : p));
      const stillOpen = table.some((p) => !p.defend);
      return {
        ...s, hands, table,
        phase: stillOpen ? "defend" : "attack",
        activeIdx: stillOpen ? 0 : s.attackerIdx,
        message: stillOpen ? "Ещё есть незакрытые карты." : "Отбился! Можно подкинуть ещё.",
      };
    });
  }, []);

  const playerTake = useCallback(() => {
    setState((s) => {
      if (s.activeIdx !== 0 || s.phase !== "defend" || s.defenderIdx !== 0) return s;
      const taken = s.table.flatMap((p) => [p.attack, p.defend].filter(Boolean));
      const hand = [...s.hands[0], ...taken];
      const hands = s.hands.map((h, i) => (i === 0 ? hand : h));
      const newAttackerIdx = nextActive(hands, s.deck, s.defenderIdx);
      const newDefenderIdx = nextActive(hands, s.deck, newAttackerIdx);
      const s2 = {
        ...s, hands, table: [], phase: "attack",
        attackerIdx: newAttackerIdx, defenderIdx: newDefenderIdx, activeIdx: newAttackerIdx,
        message: "Ты забрал карты. Ход переходит дальше.",
      };
      return refill(s2);
    });
  }, [refill]);

  const playerPass = useCallback(() => {
    setState((s) => {
      if (s.activeIdx !== 0 || s.phase !== "attack" || s.attackerIdx !== 0) return s;
      if (s.table.length === 0) return s;
      const allDefended = s.table.every((p) => p.defend);
      if (!allDefended) return s;
      const newAttackerIdx = s.defenderIdx;
      const newDefenderIdx = nextActive(s.hands, s.deck, newAttackerIdx);
      const s2 = {
        ...s, table: [], phase: "attack",
        attackerIdx: newAttackerIdx, defenderIdx: newDefenderIdx, activeIdx: newAttackerIdx,
        message: "Бита.",
      };
      return refill(s2);
    });
  }, [refill]);

  // --- Bot behavior: any non-human seat (activeIdx !== 0) acts via this tick ---
  const botTick = useCallback(() => {
    setState((s) => {
      const idx = s.activeIdx;
      if (idx === 0 || s.gameOver !== null) return s;

      if (s.phase === "defend" && idx === s.defenderIdx) {
        const openPair = s.table.find((p) => !p.defend);
        if (!openPair) return s;
        const hand = s.hands[idx];
        const candidates = hand
          .filter((c) => beats(openPair.attack, c, s.trump))
          .sort((a, b) => RANK_VALUE[a.rank] - RANK_VALUE[b.rank]);
        if (candidates.length === 0) {
          // takes all cards on the table
          const taken = s.table.flatMap((p) => [p.attack, p.defend].filter(Boolean));
          const newHand = [...hand, ...taken];
          const hands = s.hands.map((h, i) => (i === idx ? newHand : h));
          const newAttackerIdx = nextActive(hands, s.deck, idx);
          const newDefenderIdx = nextActive(hands, s.deck, newAttackerIdx);
          const s2 = {
            ...s, hands, table: [], phase: "attack",
            attackerIdx: newAttackerIdx, defenderIdx: newDefenderIdx, activeIdx: newAttackerIdx,
            message: "Бот забрал карты.",
          };
          return refill(s2);
        }
        const chosen = candidates[0];
        const newHand = hand.filter((c) => c.id !== chosen.id);
        const hands = s.hands.map((h, i) => (i === idx ? newHand : h));
        const table = s.table.map((p) => (p === openPair ? { ...p, defend: chosen } : p));
        const stillOpen = table.some((p) => !p.defend);
        return {
          ...s, hands, table,
          phase: stillOpen ? "defend" : "attack",
          activeIdx: stillOpen ? idx : s.attackerIdx,
          message: stillOpen ? "Бот отбивается дальше…" : "Бот отбился.",
        };
      }

      if (s.phase === "attack" && idx === s.attackerIdx) {
        const openRanks = openRanksOf(s.table);
        const hand = s.hands[idx];
        let card;
        if (s.table.length === 0) {
          card = [...hand].sort((a, b) => RANK_VALUE[a.rank] - RANK_VALUE[b.rank])[0];
        } else {
          card = hand.find((c) => openRanks.has(c.rank));
        }
        if (!card || s.table.length >= 6) {
          const s2 = { ...s, table: [], phase: "attack", activeIdx: idx, message: "Бита." };
          // pass turn: attacker becomes the defender, next becomes new defender
          const newAttackerIdx = s.defenderIdx;
          const newDefenderIdx = nextActive(s.hands, s.deck, newAttackerIdx);
          return refill({ ...s2, attackerIdx: newAttackerIdx, defenderIdx: newDefenderIdx, activeIdx: newAttackerIdx });
        }
        const newHand = hand.filter((c) => c.id !== card.id);
        const hands = s.hands.map((h, i) => (i === idx ? newHand : h));
        const table = [...s.table, { attack: card, defend: null, attackerIdx: idx }];
        return { ...s, hands, table, phase: "defend", activeIdx: s.defenderIdx, message: "Отбивайся!" };
      }

      // podkidnoy: a non-attacker, non-defender bot may throw in while defense is pending
      if (s.phase === "defend" && s.mode === "podkidnoy" && idx !== s.defenderIdx) {
        const openRanks = openRanksOf(s.table);
        const hand = s.hands[idx];
        const card = hand.find((c) => openRanks.has(c.rank));
        if (card && s.table.length < 6) {
          const newHand = hand.filter((c) => c.id !== card.id);
          const hands = s.hands.map((h, i) => (i === idx ? newHand : h));
          const table = [...s.table, { attack: card, defend: null, attackerIdx: idx }];
          return { ...s, hands, table, message: "Бот подкинул." };
        }
      }
      return s;
    });
  }, [refill]);

  useEffect(() => {
    if (state.gameOver !== null) return;
    if (state.deck.length === 0) {
      const emptyIdx = state.hands.map((h, i) => (h.length === 0 ? i : -1)).filter((i) => i >= 0);
      if (emptyIdx.length >= state.hands.length) {
        setState((s) => (s.gameOver !== null ? s : { ...s, gameOver: "draw" }));
      } else if (emptyIdx.length === state.hands.length - 1) {
        // everyone but one is out — that one is the durak
        const loserIdx = state.hands.findIndex((h) => h.length > 0);
        setState((s) => (s.gameOver !== null ? s : { ...s, gameOver: loserIdx }));
      }
    }
  }, [state.hands, state.deck.length, state.gameOver]);

  return {
    state, playerAttack, playerThrowIn, playerTransfer, playerDefend, playerTake, playerPass, botTick,
    reset: () => setState(init(mode, botCount, cheating)),
  };
}

function useBotTicker(state, botTick, humanCanAct) {
  useEffect(() => {
    if (state.gameOver !== null) return;
    if (state.activeIdx === 0) return; // human's own turn — never auto-advance
    if (humanCanAct) return; // human has a throw-in window open — wait for them
    const t = setTimeout(botTick, 1400);
    return () => clearTimeout(t);
  }, [state.activeIdx, state.phase, state.table, state.gameOver, humanCanAct, botTick]);
}

/* ---------------- Main ---------------- */
export default function DurakDS() {
  const [mode, setMode] = useState(null);
  const [botCount, setBotCount] = useState(null);
  const [cheating, setCheating] = useState(false);

  if (!mode) {
    return <ModeSelect onPick={setMode} />;
  }
  if (!botCount) {
    return (
      <BotCountSelect
        mode={mode}
        cheating={cheating}
        onToggleCheating={() => setCheating((c) => !c)}
        onPick={setBotCount}
        onBack={() => setMode(null)}
      />
    );
  }
  return (
    <GameScreen
      mode={mode}
      botCount={botCount}
      cheating={cheating}
      onChangeMode={() => { setMode(null); setBotCount(null); setCheating(false); }}
    />
  );
}

function ModeSelect({ onPick }) {
  return (
    <div style={{
      minHeight: "100vh", background: SHELL, display: "flex", flexDirection: "column",
      alignItems: "center", justifyContent: "center", padding: "32px 16px",
      fontFamily: "'Helvetica Neue', Arial, sans-serif",
    }}>
      <div style={{
        background: SHELL_DARK, borderRadius: 18, padding: "18px 18px 26px",
        boxShadow: "0 1px 0 rgba(255,255,255,0.6) inset, 0 8px 24px rgba(0,0,0,0.15)",
      }}>
        <DSFrame label="ДУРАК DS">
          <div style={{
            width: 340, height: 255, background: PAPER, display: "flex",
            flexDirection: "column", alignItems: "center", justifyContent: "center",
          }}>
            <div style={{ fontSize: 34, fontWeight: 700, letterSpacing: "-0.01em", color: INK, lineHeight: 1 }}>ДУРАК</div>
            <div style={{ width: 48, height: 3, background: ACCENT, margin: "14px 0" }} />
            <div style={{ fontSize: 11, letterSpacing: "0.1em", color: INK, opacity: 0.6 }}>ВЫБЕРИ РЕЖИМ НА НИЖНЕМ ЭКРАНЕ</div>
          </div>
        </DSFrame>

        <div style={{ height: 34 }} />

        <DSFrame label="РЕЖИМ ИГРЫ">
          <div style={{ width: 340, height: 255, background: PAPER, display: "flex" }}>
            <ModeCard label="ОБЫЧНЫЙ" desc="Без подкидывания" onClick={() => onPick("classic")} />
            <div style={{ width: 1, background: INK, opacity: 0.15 }} />
            <ModeCard label="ПОДКИДНОЙ" desc="Можно подкидывать карты" accent onClick={() => onPick("podkidnoy")} />
            <div style={{ width: 1, background: INK, opacity: 0.15 }} />
            <ModeCard label="ПЕРЕВОДНОЙ" desc="Перевод хода (нужно ≥2 бота)" onClick={() => onPick("perevodnoy")} />
          </div>
        </DSFrame>
      </div>
    </div>
  );
}

function ModeCard({ label, desc, accent, onClick }) {
  const [hovered, setHovered] = useState(false);
  return (
    <button
      onClick={onClick}
      onMouseEnter={() => setHovered(true)}
      onMouseLeave={() => setHovered(false)}
      onTouchStart={() => setHovered(true)}
      onTouchEnd={() => setHovered(false)}
      style={{
        flex: 1, border: "none", background: hovered ? (accent ? ACCENT : INK) : PAPER,
        display: "flex", flexDirection: "column", alignItems: "center", justifyContent: "center",
        gap: 8, cursor: "pointer", transition: "background 120ms ease",
        fontFamily: "'Helvetica Neue', Arial, sans-serif",
      }}
    >
      <div style={{
        width: 38, height: 52, border: `2px solid ${hovered ? PAPER : INK}`,
        display: "flex", alignItems: "center", justifyContent: "center",
        transform: hovered ? "translateY(-4px) scale(1.05)" : "none",
        transition: "transform 120ms ease",
      }}>
        <div style={{ width: 12, height: 12, borderRadius: accent ? 0 : "50%", background: hovered ? PAPER : INK,
          transform: accent ? "rotate(45deg)" : "none" }} />
      </div>
      <div style={{ fontSize: 11, fontWeight: 700, letterSpacing: "0.03em", color: hovered ? PAPER : INK, textAlign: "center" }}>{label}</div>
      <div style={{ fontSize: 8, letterSpacing: "0.02em", color: hovered ? PAPER : INK, opacity: 0.6, textAlign: "center", padding: "0 8px" }}>{desc}</div>
    </button>
  );
}

function BotCountSelect({ mode, cheating, onToggleCheating, onPick, onBack }) {
  const minBots = mode === "perevodnoy" ? 2 : 1;
  const options = [1, 2, 3].filter((n) => n >= minBots);
  return (
    <div style={{
      minHeight: "100vh", background: SHELL, display: "flex", flexDirection: "column",
      alignItems: "center", justifyContent: "center", padding: "32px 16px",
      fontFamily: "'Helvetica Neue', Arial, sans-serif",
    }}>
      <div style={{
        background: SHELL_DARK, borderRadius: 18, padding: "18px 18px 26px",
        boxShadow: "0 1px 0 rgba(255,255,255,0.6) inset, 0 8px 24px rgba(0,0,0,0.15)",
      }}>
        <DSFrame label={mode === "podkidnoy" ? "ПОДКИДНОЙ" : mode === "perevodnoy" ? "ПЕРЕВОДНОЙ" : "ОБЫЧНЫЙ"}>
          <div style={{
            width: 340, height: 255, background: PAPER, display: "flex",
            flexDirection: "column", alignItems: "center", justifyContent: "center",
          }}>
            <div style={{ fontSize: 22, fontWeight: 700, letterSpacing: "-0.01em", color: INK, lineHeight: 1.2, textAlign: "center", padding: "0 20px" }}>
              СКОЛЬКО БОТОВ?
            </div>
            <div style={{ width: 48, height: 3, background: ACCENT, margin: "14px 0" }} />
            {mode === "perevodnoy" && (
              <div style={{ fontSize: 10, letterSpacing: "0.06em", color: INK, opacity: 0.6, textAlign: "center", padding: "0 24px" }}>
                ПЕРЕВОДУ НУЖНО МИНИМУМ 2 ПРОТИВНИКА
              </div>
            )}
          </div>
        </DSFrame>

        <div style={{ height: 34 }} />

        <DSFrame label="КОЛИЧЕСТВО БОТОВ">
          <div style={{ width: 340, height: 255, background: PAPER, display: "flex" }}>
            {options.map((n, i) => (
              <React.Fragment key={n}>
                {i > 0 && <div style={{ width: 1, background: INK, opacity: 0.15 }} />}
                <ModeCard label={String(n)} desc={n === 1 ? "1 противник" : `${n} противника`} accent={n === options[options.length - 1]} onClick={() => onPick(n)} />
              </React.Fragment>
            ))}
          </div>
        </DSFrame>

        <button
          onClick={onToggleCheating}
          style={{
            width: "100%", marginTop: 14, padding: "10px 14px", border: `1px solid ${INK}`,
            background: cheating ? ACCENT : PAPER, color: cheating ? PAPER : INK,
            display: "flex", alignItems: "center", justifyContent: "space-between",
            fontFamily: "'Helvetica Neue', Arial, sans-serif", cursor: "pointer",
          }}
        >
          <span style={{ fontSize: 11, letterSpacing: "0.04em", fontWeight: 700 }}>ЖУЛЬНИЧЕСТВО</span>
          <span style={{ fontSize: 10, letterSpacing: "0.03em", opacity: 0.8 }}>
            {cheating ? "ВКЛ · любая карта подкидывается" : "ВЫКЛ"}
          </span>
        </button>

        <div style={{ display: "flex", justifyContent: "center", marginTop: 14 }}>
          <button onClick={onBack} style={{ ...btnStyle(true), padding: "6px 16px", fontSize: 10 }}>Назад</button>
        </div>
      </div>
    </div>
  );
}

function GameScreen({ mode, botCount, cheating, onChangeMode }) {
  const { state, playerAttack, playerThrowIn, playerTransfer, playerDefend, playerTake, playerPass, botTick, reset } =
    useDurakGame(mode, botCount, cheating);

  const isHumanTurn = state.activeIdx === 0;
  const isHumanDefender = state.defenderIdx === 0;
  const isHumanAttacker = state.attackerIdx === 0;
  const openPair = state.table.find((p) => !p.defend);
  const anyDefended = state.table.some((p) => p.defend);

  const canPass = isHumanTurn && isHumanAttacker && state.phase === "attack" && state.table.length > 0 && state.table.every((p) => p.defend);
  const canTake = isHumanTurn && isHumanDefender && state.phase === "defend" && !!openPair;
  const openRanksSet = new Set(state.table.flatMap((p) => [p.attack.rank, p.defend?.rank].filter(Boolean)));
  const hasThrowableCard = state.cheating ? state.hands[0].length > 0 : state.hands[0].some((c) => openRanksSet.has(c.rank));
  const rawCanThrowIn = state.mode === "podkidnoy" && state.phase === "defend" && !isHumanDefender
    && state.table.length < 6 && hasThrowableCard;
  const canTransfer = state.mode === "perevodnoy" && isHumanTurn && isHumanDefender && state.phase === "defend" && !!openPair && !anyDefended;

  // Track whether the human has explicitly waved off the current throw-in
  // window, so the bot doesn't race a timer against them.
  const [skippedKey, setSkippedKey] = useState(null);
  const windowKey = `${state.table.length}-${state.defenderIdx}-${state.hands[0].length}`;
  const canThrowIn = rawCanThrowIn && skippedKey !== windowKey;

  useBotTicker(state, botTick, canThrowIn);

  const handSelect = (cardId) => {
    const card = state.hands[0].find((c) => c.id === cardId);
    if (!card) return;
    if (isHumanTurn && isHumanAttacker && state.phase === "attack") {
      playerAttack(card);
    } else if (isHumanTurn && isHumanDefender && state.phase === "defend") {
      if (canTransfer && card.rank === openPair.attack.rank) {
        playerTransfer(card);
      } else {
        playerDefend(card);
      }
    } else if (canThrowIn) {
      playerThrowIn(card);
    }
  };

  const handSelectable = state.gameOver === null && (
    (isHumanTurn && (isHumanAttacker || isHumanDefender)) || canThrowIn
  );

  const isFresh = state.table.length === 0 && state.hands.every((h) => h.length === 6);

  const modeLabel = mode === "podkidnoy" ? "ПОДКИДНОЙ" : mode === "perevodnoy" ? "ПЕРЕВОДНОЙ" : "ОБЫЧНЫЙ";
  const opponentCounts = state.hands.slice(1).map((h) => h.length);

  let statusMessage = state.message;
  if (state.gameOver !== null && state.gameOver !== undefined) {
    if (state.gameOver === "draw") statusMessage = "НИЧЬЯ";
    else if (state.gameOver === 0) statusMessage = "ТЫ ПРОИГРАЛ";
    else statusMessage = "ТЫ ВЫИГРАЛ";
  } else if (canTransfer) {
    statusMessage = "Отбейся или переведи карту той же масти";
  } else if (canThrowIn) {
    statusMessage = "Можешь подкинуть карту";
  }

  return (
    <div style={{
      minHeight: "100vh", background: SHELL, display: "flex", flexDirection: "column",
      alignItems: "center", justifyContent: "center", padding: "32px 16px",
      fontFamily: "'Helvetica Neue', Arial, sans-serif",
    }}>
      <div style={{
        background: SHELL_DARK, borderRadius: 18, padding: "18px 18px 26px",
        boxShadow: "0 1px 0 rgba(255,255,255,0.6) inset, 0 8px 24px rgba(0,0,0,0.15)",
      }}>
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", padding: "0 4px 8px" }}>
          <span style={{ fontSize: 10, letterSpacing: "0.06em", color: INK, opacity: 0.6 }}>
            {modeLabel} · {botCount} {botCount === 1 ? "БОТ" : "БОТА"}{cheating ? " · ЖУЛЬНИЧЕСТВО" : ""}
          </span>
          {(isFresh || state.gameOver !== null) && (
            <button onClick={onChangeMode} style={{ ...btnStyle(true), padding: "4px 10px", fontSize: 10 }}>Сменить режим</button>
          )}
        </div>

        <DSFrame label={`КОЗЫРЬ — ${state.trumpCard ? SUIT_LABEL[state.trumpCard.suit].toUpperCase() : ""}`}>
          <TableScreen trumpCard={state.trumpCard} deckCount={state.deck.length} table={state.table} />
        </DSFrame>

        <div style={{ height: 34, display: "flex", alignItems: "center", justifyContent: "space-between", padding: "0 4px" }}>
          <span style={{ fontSize: 11, letterSpacing: "0.04em", color: INK }}>КОЛОДА: {state.deck.length}</span>
          <span style={{ fontSize: 11, letterSpacing: "0.04em", color: INK, maxWidth: 200, textAlign: "center" }}>
            {statusMessage}
          </span>
          <span style={{ fontSize: 11, letterSpacing: "0.04em", color: INK }}>
            {opponentCounts.map((n, i) => `Б${i + 1}:${n}`).join("  ")}
          </span>
        </div>

        <DSFrame label="ТВОЯ РУКА">
          <HandScreen hand={state.hands[0]} onSelect={handSelect} selectable={handSelectable} />
        </DSFrame>

        {rawCanThrowIn && (
          <div style={{
            marginTop: 8, padding: "6px 10px", background: PAPER, border: `1px solid ${ACCENT}`,
            fontSize: 10, letterSpacing: "0.04em", color: ACCENT, textAlign: "center",
          }}>
            {state.cheating ? "ЖУЛЬНИЧЕСТВО: МОЖНО ПОДКИНУТЬ ЛЮБУЮ КАРТУ" : "МОЖНО ПОДКИНУТЬ КАРТУ ТОГО ЖЕ РАНГА"}
          </div>
        )}

        <div style={{ display: "flex", gap: 8, marginTop: 14, justifyContent: "center" }}>
          <button onClick={playerPass} disabled={!canPass} style={btnStyle(canPass)}>Бита</button>
          <button onClick={playerTake} disabled={!canTake} style={btnStyle(canTake, true)}>Взять</button>
          {canThrowIn && (
            <button onClick={() => setSkippedKey(windowKey)} style={btnStyle(true)}>Не подкидываю</button>
          )}
          {state.gameOver !== null && <button onClick={reset} style={btnStyle(true)}>Заново</button>}
        </div>
      </div>
    </div>
  );
}

function btnStyle(enabled, accent = false) {
  return {
    fontFamily: "'Helvetica Neue', Arial, sans-serif", fontSize: 12, letterSpacing: "0.04em",
    padding: "8px 18px", background: enabled ? (accent ? ACCENT : INK) : "#B9B6AE",
    color: PAPER, border: "none", cursor: enabled ? "pointer" : "default",
    opacity: enabled ? 1 : 0.6, transition: "opacity 120ms ease",
  };
}
