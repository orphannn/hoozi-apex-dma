<div align="center">
  
<h1>HOOZi APEX V3</h1>
<img width="1939" height="811" alt="24c1b41a3f3f7c7e8dbed5744280b1da" src="https://github.com/user-attachments/assets/2cd0bd16-f687-404c-859e-1e03d42899e8" />

<p><strong>ESP &nbsp;|&nbsp; Smart Items &nbsp;|&nbsp; Throw Assist &nbsp;|&nbsp; Lua Scripts</strong></p>

<p>
  An all-in-one Apex Legends assistant focused on visual information,
  configuration flexibility and an extensible Lua scripting system.
</p>

## Open Source / 开源

Writeups and sanitized reference code from our internals.
从内部整理出的技术文章与脱敏参考代码。

👉 **[Browse / 浏览](./opensource/)**

</div>

<p align="center">
  <a href="https://discord.gg/PnfR95ADW">
    <img
      src="https://img.shields.io/badge/Discord-7289DA?style=for-the-badge&logo=discord&logoColor=white"
      alt="Discord"
    >
  </a>
  <a href="https://www.hoozi.cc">
    <img
      src="https://img.shields.io/badge/Website-0077B6?style=for-the-badge"
      alt="Website"
    >
  </a>
  <a href="https://docs.hoozi.cc/en/products/apex/guide/">
    <img
      src="https://img.shields.io/badge/Documentation-222222?style=for-the-badge&logo=readthedocs&logoColor=white"
      alt="Documentation"
    >
  </a>
</p>

---

## Exclusives

### Map Collision Models ("Miss-Hit")

Map model collision automatically searches the remaining body parts when part of a character's bones are blocked. This behavior is commonly referred to as **miss-hit**.

<p align="center">
  <img
    width="580"
    alt="Map Collision Models"
    src="https://github.com/user-attachments/assets/55074d40-ef8f-4d3b-8214-18bc91e99b77"
  />
</p>

---

## Features

<p align="center">
  <img
    width="4200"
    height="3200"
    alt="HApex Features"
    src="https://github.com/user-attachments/assets/890bf702-4ef3-4177-aa5c-e25c1081d134"
  />
</p>

```text
Aimbot
├── Aim
│   ├── Master switches / Current weapon / Basic (hotkey · aim on fire · visible check · ignore team/downed/roster)
│   ├── FOV (dead zone · hip/ADS FOV · distance/zoom shrink)
│   ├── Body part (head/neck/upper body/full body · exclude head)
│   ├── Target (closest to crosshair/distance/ADS crosshair+hip distance/weighted priority · sticky lock · lost-target grace · switch lockout · switch delay · max range)
│   ├── Timing (lock during cooldown · hard turn reaction · teleport no-lock)
│   ├── No recoil (X / Y strength)
│   └── Model aim (drone / overdrive drone / gas tank / jump pad / electric fence / trophy)
├── Smooth (hip X/Y · ADS X/Y)
├── Smoothness algorithm (normal ramp / magnet / curve)
├── Movement algorithm (normal proportional / PID: Kp · Ki · Kd deteriorate · damp)
└── Trigger (master · basic · release · timing · smooth · algorithm)
    └── Throw (grenade point · auto throw · damage range · trajectory)

Visuals
├── Player (ESP drawing · glow · 4 per-state scenarios · optimize)
└── Enhance (aim predict point · mini map radar · big map radar · off-screen indicator)

Items
├── Ground items (master · stacking · text · icon · colors · smart items/smart settings/backpack management)
└── Entity models (death box · traps · ultimates)

Scripts       — Lua script management / auto-load
Misc          — watermark · perf monitor · keybinds · spectators · heirloom · ALGS radar · web radar

Configs       — reload · combat · bunnyhop · superglide · mantle boost · auto grapple · tap strafe
Rosters       — add · import/export · cloud settings
USettings     — menu · draw · theme · display · device (input device rate) · license · notice · dev tools
```

---

## Showcase

<p align="center">
  <a href="https://www.youtube.com/watch?v=qYd1LjixROA">
    <img
      src="https://img.youtube.com/vi/qYd1LjixROA/maxresdefault.jpg"
      width="800"
      alt="HApex Demo"
    />
  </a>
</p>

<p align="center">
  <sub>Click the preview above to watch the HApex demonstration on YouTube.</sub>
</p>

---

<div align="center">

## 🚀 Get Started with HApex

### 🎁 **FREE TRIAL**

## **Plans starting at $15/month**

<p>
  Download the client and start your free trial today.
</p>

<p>
  <a href="https://www.hoozi.cc/shop">
    <img
      src="https://img.shields.io/badge/Buy_Now-0077B6?style=for-the-badge"
      alt="Buy Now"
    >
  </a>
</p>

</div>

---

<div align="center">

**HApex V3**

<a href="https://www.hoozi.cc">Website</a>
 •  <a href="https://docs.hoozi.cc/en/products/apex/guide/">Documentation</a>
 •  <a href="https://discord.gg/PnfR95ADW">Discord</a>

</div>
