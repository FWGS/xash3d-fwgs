# Fake lag (choke de pacotes)

Implementacao de fakelag no envio de pacotes do client (`engine/client/cl_main.c`).
Os pacotes de usercmd ficam segurados por um tempo configuravel antes de ir para o
socket, o que aumenta o ping visto pelo servidor - usado em conjunto com o backtrack
do client (`cl_bt_sync_fakelag`).

## CVars

| CVar | Default | Descricao |
| --- | --- | --- |
| `cl_fakelag` | `0` | liga/desliga |
| `cl_fakelag_limit` | `60` | tempo maximo segurando o pacote, em ms |
| `cl_fakelag_type` | `1` | `0` dinamico, `1` maximo, `2` flutuante |
| `cl_fakelag_move` | `0` | `0` sempre, `1` so parado, `2` so no ar |
| `cl_fakelag_while_shooting` | `0` | segura tambem enquanto atira |
| `cl_fakelag_variance` | `20` | variacao do modo dinamico, em % |
| `cl_fakelag_debug` | `0` | estatisticas no console |

## Como funciona

- `CL_HoldPacket()` decide se o pacote atual fica retido.
- Estado por client: `cl_fakelag_next`, `cl_fakelag_toggle`, `cl_fakelag_held`,
  `cl_fakelag_sent`, `cl_fakelag_lastprint`.
- `cl_fakelag_type 0` (dinamico) usa `cl_fakelag_variance` para variar o tempo.
- `cl_fakelag_type 2` (flutuante) alterna o tempo entre os envios.
- `cl_fakelag_move` limita o fakelag a parado ou no ar, para nao atrapalhar movimento.
- Com `cl_fakelag_while_shooting 0`, o pacote com `IN_ATTACK` sai na hora.

## Interacao com o backtrack do client

O backtrack do `cs16-client-Enchanced` pode ajustar `cl_fakelag_limit`,
`cl_fakelag_type` e `cl_fakelag` sozinho (`cl_bt_sync_fakelag 1`), mas apenas enquanto o
jogador esta atirando, e devolve os valores originais ao parar. Sem isso o servidor nao
volta a posicao do alvo e o tiro nao registra em alvo em movimento.

## Aviso

Fakelag ligado direto (fora do tiro) deixa o movimento impreciso e pode causar
"deslizamento" visual, porque o client continua prevendo enquanto o servidor nao
confirma. Prefira `cl_fakelag_move 1` ou deixar o sincronismo do backtrack cuidar disso.
