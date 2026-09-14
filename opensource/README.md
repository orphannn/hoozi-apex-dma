<h1 align="center">HOOZi · Open Source</h1>
<p align="center">Writeups and sanitized reference code · 技术拆解与脱敏参考代码</p>

---

Each row below is one topic folder — a writeup, the diagrams that go with it, and a sanitized single-file reference. Everything is relative-linked inside its own folder; copy a topic out with `cp -r` and it still works.

每一行 = 一个 topic 子文件夹,含一篇文章 + 配套图 + 脱敏参考实现。文件夹内相对路径互相引用,`cp -r` 拷走照样能读。

## Index / 目录

| # | Topic | Writeup | Reference code | LOC | Summary |
|---|---|---|---|---|---|
| 1 | Heirloom Animation | [📖](./heirloom-animation/) | [heirloom_anim_reference.cpp](./heirloom-animation/heirloom_anim_reference.cpp) | 685 | Apex non-owner heirloom mesh + native anim via `cache_A` pollution + FSM force-write seq. Ceiling of what pure external DMA can do on this engine. 非拥有者传家宝天花板 + 上线方案。 |

## Folder layout / 目录规范

```
opensource/
├── README.md                              ← this index / 表格索引
└── <topic-slug>/                          ← one folder per topic
    ├── README.md                          ← the writeup (GitHub auto-renders)
    ├── <name>_reference.cpp               ← sanitized code
    └── img/                               ← diagrams / screenshots
        ├── fig1-*.png
        └── ...
```

- `<topic-slug>` — lowercase kebab-case, one topic per folder.
- Nothing shared between topics; each folder is a standalone drop.
- Adding a new topic: create the folder, add a row to the table above.

## Notes / 说明

- **Reference code is not a working build.** Every drop assumes a DMA reader API declared at the top of the file (`namespace dma { Read / Write / *Scatter* }`). Swap in your own.
- **Offsets are game-version specific.** Refresh via your dumper each patch.
- **License**: MIT — use, adapt, ship. Attribution appreciated, not required.
