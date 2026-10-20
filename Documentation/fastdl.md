# FastDL: resourcelist e downloader HTTP(S)

Como esta engine baixa recursos de servidores GoldSrc/ReHLDS que usam `sv_downloadurl`
(FastDL). Cobre o parser de `svc_resourcelist`, o downloader com TLS e as
descompressoes `.bz2`/`.gz`.

## Fluxo

1. O servidor envia `svc_resourcelist` (ou `svc_resourcelocation` com a URL do FastDL).
2. `engine/client/cl_parse.c` le a lista e preenche `cl.resourcesonhand[]`.
3. `engine/client/cl_main.c` precacheia; o que falta vai para a fila de download.
4. `engine/common/http/net_http_xash.c` baixa, descomprime e grava em `cstrike/downloaded/`.
5. Se o arquivo nao existir no FastDL, o downloader tenta `arquivo.bz2`/`arquivo.gz` e,
   em ultimo caso, o proprio servidor de jogo.

## Parser de `svc_resourcelist`

O formato nao e' documentado e varia entre GoldSrc, ReHLDS e builds antigas. O parser
em `cl_parse.c` e' tolerante de proposito:

- Descobre o layout por pontuacao (`CL_ResScoreLayout`): testa larguras de campo e exige
  que todos os nomes seguidos de tamanho/fechamento facam sentido.
- Aceita `rguc_reserved` de 32 bytes (padrao GoldSrc) e o count em 12 bits.
- Remove prefixos de path (`CL_ResStripPrefix`), inclusive o `!`/`!A` que o ReHLDS usa
  para recursos custom.
- Descarta nomes invalidos (`CL_ResNameIsValid`) em vez de abortar o parse.
- Se nada pontuar, imprime `resourcelist: layout desconhecido, ignorando lista` e segue
  sem a lista - o mapa ainda entra.

Funcoes principais: `CL_ResHasPathPrefix`, `CL_ResStripPrefix`, `CL_ResNameIsValid`,
`CL_ResGuessType`, `CL_ResPeekName`, `CL_ResFindNext`, `CL_ResReadEntry`,
`CL_ResScoreLayout`.

## Downloader (`net_http_xash.c`)

### TLS

- `HTTP_TlsInit()` inicializa OpenSSL sob demanda.
- `HTTP_FileTlsHandshake()` faz o handshake quando a URL e' `https://`.
- `HTTP_NetSend()`/`HTTP_NetRecv()` trocam de socket direto para SSL conforme o arquivo.
- Sem OpenSSL no build, HTTPS cai para erro explicito (nao trava).

### Descompressao

`HTTP_SelectProcessor()` escolhe o backend na ordem: zlib do sistema (se compilada),
miniz (embutido) e, por ultimo, o caminho antigo. `HTTP_FileDecompress()` decide pelo
magic number, nao pelo sufixo:

- `HTTP_GzipDataOffset()` acha o inicio do stream gzip dentro do arquivo.
- `HTTP_InflateToFile()` infla direto para o destino (sem buffer gigante em RAM).
- `HTTP_FileBunzip2()` trata `.bz2` (FastDL do ReHLDS costuma servir `.bz2`).
- `HTTP_GunzipBuffer()`/`HTTP_TryDecompress()` atendem os casos em memoria.

### Retry e nomes

- `HTTP_HasCompressedSuffix()` evita tentar `.bz2.bz2`.
- `HTTP_OutName()` calcula o nome final (sem sufixo de compressao).
- O downloader reaproveita `cstrike/downloaded/` antes de baixar de novo.

## CVars

| CVar | Default | Descricao |
| --- | --- | --- |
| `http_decompress` | `0` | `0` auto, `1` zlib, `2` miniz, `3` legado |
| `http_fastdl_suffix` | `1` | tenta `arquivo.bz2`/`.gz` quando o arquivo nao existe |
| `cl_downloadurl` | `""` | URL de FastDL usada quando o servidor nao anuncia uma |

Comandos de diagnostico:

- `http_gzip_selftest` - roda o teste interno de descompressao (`Cmd_HttpGzipSelfTest`).

## Guards de precache

Um BSP truncado ou vazio derrubava o renderer em `R_GenerateVBO`. Em `cl_main.c`:

- Arquivo de 0 byte em `downloaded/` e' apagado e baixado de novo (`cl_custom.c`).
- BSP com menos de 1024 bytes e' recusado no precache com `S_ERROR`.
- So o BSP do mapa pode ocupar o indice do world model (`Q_strstr(..., ".bsp")`).
- `RES_FATALIFMISSING` deixa de derrubar a conexao: vira warning + `ClearBits`, entao
  servidores com um recurso faltando ainda entram.

## Build

O `wscript` procura zlib e OpenSSL e define:

- `HAVE_SYSTEM_ZLIB` (linka `zlib`)
- `HAVE_SYSTEM_OPENSSL` (linka `ssl`, `crypto`)

Sem elas o build continua, usando miniz e sem HTTPS.

## Ferramentas

- `tools/decode_reslist.py` - decodifica um `buffer.dat` gravado pela engine e lista os
  recursos do `svc_resourcelist`.
- `tools/decode_reslist2.py` - decodifica `reslist_raw.bin` tentando descobrir o layout
  real (larguras de campo) por tentativa e erro.

```
python3 tools/decode_reslist.py caminho/buffer.dat
python3 tools/decode_reslist2.py caminho/reslist_raw.bin
```
