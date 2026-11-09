# AutoMiner (.levipack) - mineracao automatica para Minecraft Bedrock (Android, LeviLauncher)

Mod nativo `preload-native` (mesma estrutura do DynamicShadows 2.2.5). Gera `AutoMiner.so` e `AutoMiner.levipack`.

## O que ja esta pronto e testado (no PC, `sh tests/run.sh`)
- Templates: **2x2, 3x3, Escada (sobe/desce), Tunel (LxA)** e **area Pos1-Pos2**.
- Ordem de quebra: fatia a fatia, de cima para baixo (areia/cascalho nao cai no buraco).
- Maquina de estados: Planning -> Navigating -> Aiming -> Breaking -> Verifying -> Done, com Recovering.
- Salvaguardas: ignora ar/liquido/bedrock/lava, nao quebra bloco colado em lava/agua, nunca quebra fora da selecao,
  detecta "travado" e recalcula caminho, adia blocos inacessiveis, reconfere no fim, orcamento de CPU por tick, aborta se perder o jogador.
- Preview: outline 3D (caixa do perimetro + grade por bloco, alvo atual verde, ignorados vermelho, Pos1/Pos2) e HUD/menu na tela,
  tudo desenhado por um overlay GL proprio (projecao calculada a partir da pose do jogador; use o mesmo FOV do jogo em "FOV do outline"; so 1a pessoa).
- Menu do launcher + teclas configuraveis (padrao: M menu, N proximo modelo, Z Pos1, X Pos2, V iniciar, C parar).

## O que VOCE precisa fornecer: os bindings do jogo
O Bedrock nao exporta simbolos. As funcoes internas (posicao do jogador, getBlock, quebrar bloco, injetar input)
dependem da versao exata do `libminecraftpe.so` e **nao** foram inventadas aqui. Preencha
`autominer_bindings.cfg` (modelo em `config/autominer_bindings.cfg.example`) na pasta de config do mod
(`.../files/native_mod_configs/AutoMiner/`). Sem ele o mod carrega, o menu/preview funcionam e o HUD mostra `FALTA: ...`.
Prototipos esperados: comentario no topo de `src/NativeBridge.cpp` (se a sua versao diferir, ajuste so la).

## Build
GitHub Actions (`.github/workflows/build.yml`): suba o repo, rode o workflow, baixe o artefato `AutoMiner-Android`
(`.levipack` + `.so`). Local: `xmake f -p android -a arm64-v8a -m release --ndk=<NDK r28c> --ndk_sdkver=28 && xmake -y AutoMiner`.
Para empacotar contra o preloader real: `--preloader_lib=/caminho/libpreloader.so`.

## Estrutura
`include/Types|Templates|Planner|Executor|Settings|Render|Controller|Bridge|NativeBridge|...` e `src/` correspondentes.
Logica pura (sem jogo): Templates, Planner, Executor, Settings, Render, Controller. Ponte com o jogo: NativeBridge. Menu: Menu.cpp. Overlay: GlRenderer.cpp.

## Limites conhecidos
- Nao testado em aparelho/versao 1.26 (sem acesso ao jogo aqui): NativeBridge, GlRenderer e o formato do valor das Keybinds do menu.
- Outline pode atrasar levemente em relacao a camera (pose lida por tick); desligue o "bobbing" se incomodar.
- Mexer em movimento/mira em servidores com anticheat pode render punicao; use em mundos seus.
Licenca dos arquivos de build/vendor herdados: MIT (ver LICENSE).
