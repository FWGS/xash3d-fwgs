# Xash3D ENCHANCED Engine

Fork do Xash3D FWGS focado em Counter-Strike 1.6 no Windows e Linux, mantendo compatibilidade com servidores GoldSrc antigos e servidores Xash3D/modernos.

Este repositório remove do guia principal instruções de plataformas fora do alvo deste fork. O foco documentado aqui é:

- Windows 32-bit.
- Linux 32-bit em sistemas x86/x86_64.
- Compatibilidade com servidores antigos e novos.
- Melhor robustez para downloads, mensagens de rede, HUD e renderização.

## Alteracoes Do Fork Por Data

### 2026-10-19

#### FastDL com HTTPS

- Downloader HTTP ganhou TLS com OpenSSL (`HTTP_TlsInit`, `HTTP_FileTlsHandshake`,
  `HTTP_NetSend`/`HTTP_NetRecv`), necessario para FastDL em `https://` (ex.: `mkl.bz`).
- Build detecta zlib e OpenSSL no `wscript` (`HAVE_SYSTEM_ZLIB`, `HAVE_SYSTEM_OPENSSL`).
- Descompressao em cascata: zlib do sistema, miniz e caminho legado (`http_decompress`).
- Retry de sufixo `.bz2`/`.gz` quando o arquivo nao existe no FastDL
  (`http_fastdl_suffix`) e recuperacao do nome original no `dlfile`.
- `cl_downloadurl` permite fixar uma URL de FastDL quando o servidor nao anuncia uma.

#### Parser de `svc_resourcelist`

- Parser tolerante (`cl_parse.c`): descobre o layout por pontuacao, aceita
  `rguc_reserved` de 32 bytes e count em 12 bits, remove prefixos `!`/`!A` do ReHLDS e
  ignora nomes invalidos em vez de abortar a lista.
- Servidores ReHLDS com recursos custom (ex.: 989 recursos) entram sem falhar.

#### Precache e estabilidade

- BSP vazio/truncado e' recusado; so o `.bsp` do mapa ocupa o world model.
- Arquivo de 0 byte em `downloaded/` e' apagado e baixado de novo.
- `RES_FATALIFMISSING` virou warning: recurso faltando nao derruba mais a conexao.
- `Mod_UnloadTextures` nao chama mais `Assert(0)` em modelo invalido.
- MP3 com ID3v2.3 passa a ser aceito (antes so 2.4).

#### Fake lag

- `cl_fakelag*` no envio de pacotes: choke com modo maximo/dinamico/flutuante,
  limite em ms, variacao, restricao por movimento e opcao de segurar durante o tiro.

#### Documentacao

- `Documentation/fastdl.md` - resourcelist, TLS, descompressao, cvars e ferramentas.
- `Documentation/fakelag.md` - cvars e funcionamento do choke.
- `Documentation/client-fixes.md` - correcoes de compatibilidade.

### 2026-05-26

#### Downloads e rede

- Aumentado o paralelismo padrao do downloader HTTP/FastDL:
  - `http_maxconnections` passou de `6` para `12`.
  - `http_max_active_requests` passou de `6` para `12` quando o backend libcurl estiver disponivel.
- `cl_dlmax` agora usa `1200` como padrao, anunciando um tamanho de fragmento mais adequado para conexoes atuais.
- O fallback GoldSrc `dlfile` deixou de usar o caminho pre-active de `128 bytes`; agora usa fragmentos de ate `1024 bytes`, mantendo limite conservador para servidores antigos.
- Corrigido truncamento de caminho em `CL_CheckFile`: o buffer temporario deixou de usar `MAX_QPATH` e passou a usar `MAX_SYSPATH`, evitando problemas com caminhos como `sound/` + nome longo.
- Recursos genericos agora mantem o estado de download pendente enquanto o arquivo ainda nao terminou, evitando tentativa de precache cedo demais.

#### Tolerancia a pacotes malformados

- Mensagens de usuario invalidas, nao registradas, fora de faixa ou truncadas agora sao descartadas com log em vez de derrubar o cliente com `Host_Error`.
- Overflows em parser GoldSrc/Xash agora descartam o pacote malformado em vez de encerrar a sessao inteira.
- Isso melhora compatibilidade com servidores que enviam mensagens quebradas, plugins ruidosos ou respostas externas inesperadas.

#### Limites internos

- Aumentados limites seguros de strings internas:
  - `MAX_VA_STRING` para `4096`.
  - `MAX_SYSPATH` para `4096`.
  - `MAX_SERVERINFO_STRING` para `2048`.
  - `MAX_PRINT_MSG` para `16384`.
  - `MAX_TOKEN` para `4096`.
- `MAX_STRING` e `MAX_OSPATH` foram mantidos conservadores para nao quebrar layouts, saves e estruturas antigas.

#### Renderizacao e Canvas

- Adicionada API `Canvas` 2D no renderer GL para overlays e HUDs modernos.
- Adicionado stub seguro para renderer software.
- Corrigido estado OpenGL do Canvas para salvar/restaurar depth, culling, blend e scissor.
- Removido uso global incorreto de `Canvas_BeginFrame`/`Canvas_EndFrame` no render principal.
- Adicionado `Documentation/canvas_usage.md` com exemplos e diretrizes de uso.

#### Compatibilidade de build

- Adicionado stub `engine/avi/avi.h` para manter includes de AVI consistentes quando o modulo completo nao estiver no include path.

### 2026-05-02

Alteracoes ja documentadas anteriormente neste fork:

- `cl_walltrans 1`: atualizacao OpenGL mantida desativada por padrao para compatibilidade.
- `r_fullbright 1`: reativado para servidores dedicados.
- `aspect_ratio 0.1 - 2`: permite alterar proporcao sem alterar resolucao.
- Extensoes iniciais de buffer para reduzir risco de overflow.
- Melhorias iniciais no fluxo de renderizacao grafica.

## Instalacao E Execucao

1. Compile ou baixe os binarios deste fork.
2. Copie os binarios da engine para uma pasta de jogo.
3. Copie as pastas do Half-Life/Counter-Strike instalados legalmente, como `valve` e `cstrike`, para a mesma pasta.
4. Execute:
   - Windows: `xash3d.exe`
   - Linux: `./xash3d`

Para ver opcoes de inicializacao:

```sh
./xash3d -help
```

## Build

Este projeto usa Waf.

Nunca use o ZIP gerado pelo GitHub para build completo, porque dependencias externas/submodules podem nao vir junto. Clone com `--recursive`.

### Windows

Requisitos:

- Visual Studio.
- Python.
- Git.
- SDL2 development package para Visual Studio.

Build:

```bat
git clone --recursive https://github.com/pescadordegoiaba/XASH3D-ENCHANCED.git
cd XASH3D-ENCHANCED
waf configure --sdl2=c:/path/to/SDL2
waf build
waf install --destdir=c:/path/to/output
```

### Linux

#### Debian/Ubuntu 32-bit em sistema 64-bit

```sh
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install aptitude
sudo aptitude --without-recommends install git build-essential gcc-multilib g++-multilib libsdl2-dev:i386 libfreetype-dev:i386 libopus-dev:i386 libbz2-dev:i386 libvorbis-dev:i386 libopusfile-dev:i386 libogg-dev:i386
export PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig
git clone --recursive https://github.com/pescadordegoiaba/XASH3D-ENCHANCED.git
cd XASH3D-ENCHANCED
./waf configure
./waf build
```

#### Fedora

```sh
sudo dnf install git gcc gcc-c++ glibc-devel.i686 SDL3-devel.i686 sdl2-compat-devel.i686 opus-devel.i686 freetype-devel.i686 bzip2-devel.i686 libvorbis-devel.i686 opusfile-devel.i686 libogg-devel.i686
export PKG_CONFIG_PATH=/usr/lib/pkgconfig
git clone --recursive https://github.com/pescadordegoiaba/XASH3D-ENCHANCED.git
cd XASH3D-ENCHANCED
./waf configure
./waf build
```

#### Arch/Manjaro

Habilite `multilib` em `/etc/pacman.conf`:

```ini
[multilib]
Include = /etc/pacman.d/mirrorlist
```

Instale dependencias comuns:

```sh
sudo pacman -Syu
sudo pacman -S git base-devel python sdl2 lib32-sdl2 lib32-mesa lib32-libglvnd lib32-freetype2 lib32-bzip2 lib32-libvorbis lib32-libogg
git clone --recursive https://github.com/pescadordegoiaba/XASH3D-ENCHANCED.git
cd XASH3D-ENCHANCED
./waf configure
./waf build
```

## CVars Relevantes Do Fork

```cfg
http_maxconnections 12
http_max_active_requests 12
cl_dlmax 1200
cl_walltrans 0
r_fullbright 1
aspect_ratio 1
```

## Reportando Problemas

Ao reportar um bug, informe:

- Sistema operacional.
- Se o binario e 32-bit ou 64-bit.
- Mapa e servidor usado.
- Log completo do console.
- Valor de `cl_dlmax`, `http_maxconnections` e `http_max_active_requests` se o problema envolver download.
