# Correcoes de compatibilidade do client

Ajustes pontuais nesta engine/fork para nao derrubar o client com conteudo real de
servidores CS 1.6.

## ID3v2.3 em MP3 (`engine/client/soundlib/snd_mp3.c`)

Antes so `ID3v2.4` era aceito e musicas com `ID3v2.3` (a maioria dos `sound/music`
custom) falhavam com `invalid ID3v2 tag version`. As duas versoes usam o mesmo encoding
syncsafe, entao agora `major_ver` aceita 3 e 4.

## `Mod_UnloadTextures` com modelo invalido (`ref/gl/gl_context.c`)

O `default:` do `switch` chamava `Assert(0)`. Um arquivo corrompido (ou um BSP vazio que
passou pelo precache) chegava ali como `mod_bad` e derrubava o client inteiro. Agora o
caso nao faz nada: nao ha textura para descarregar.

## Arquivo de 0 byte (`engine/client/cl_custom.c`)

Resto de download interrompido ficava em `downloaded/` com tamanho 0 e o precache
carregava lixo. Agora `FS_FileExists` + `FS_FileSize <= 0` faz o arquivo ser apagado e
baixado de novo.

## BSP truncado no precache (`engine/client/cl_main.c`)

Ver `fastdl.md`: BSP com menos de 1024 bytes e' recusado, world model so aceita `.bsp`,
e `RES_FATALIFMISSING` vira warning em vez de disconnect.

## zlib/OpenSSL no build

`wscript` detecta `zlib` e `OpenSSL` (`HAVE_SYSTEM_ZLIB`, `HAVE_SYSTEM_OPENSSL`) e
`engine/wscript` linka as libs quando presentes. Sem elas o build funciona, mas perde
HTTPS e a descompressao mais rapida.
