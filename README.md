# TP2 - Sincronização em Sistemas Distribuídos: Token Ring (A3)

Implementação distribuída em linguagem C e Docker do algoritmo **Token Ring (Opção A3 - Trilha A)** para controle de concorrência e exclusão mútua sobre um recurso compartilhado hipotético $R$, desenvolvida para a disciplina **BCC362 - Sistemas Distribuídos** (UFOP), ministrada pelo Prof. Joubert Lima

## 1. Visão Geral e Arquitetura

O sistema coordena acessos concorrentes de escrita ao recurso $R$ sem utilizar coordenador central fixo, memória compartilhada ou *locks* locais do sistema operacional entre os nós. A arquitetura é composta por 11 contêineres isolados que se comunicam exclusivamente via rede TCP/IP

* **5 Clientes (`client0` a `client4`):** Processos independentes que disparam entre 10 e 50 requisições de acesso ao recurso $R$. Cada cliente conhece e se comunica exclusivamente com um único gerenciador do *Cluster Sync*
* **5 Gerenciadores do Cluster Sync (`node0` a `node4`):** Processos organizados em topologia de anel lógico fechado ($P_0 \to P_1 \to P_2 \to P_3 \to P_4 \to P_0$). Executam o protocolo de passagem de *token* e separam a sincronização (`acquire` e `release`) da execução da seção crítica simulada.
* **1 Observador / Verificador (`observer`):** Processo de instrumentação passiva que registra os eventos `BEGIN` e `END` de todos os nós. Monitora o número de processos ativos em $R$, acusa violações de exclusão mútua sem bloquear os participantes e consolida as métricas finais.

## 2. Estrutura do Repositório

```text
tp2-token-ring/
├── include/
│   └── common.h          # Protocolo de rede, structs de mensagens e relogio monotonico
├── src/
│   ├── observer.c        # Verificador automatico de propriedades e gerador de relatorio
│   ├── node.c            # Gerenciador do Cluster Sync (acquire, release e anel logico)
│   └── client.c          # Gerador de carga parametrizavel por variaveis de ambiente
├── logs/                 # Logs gerados automaticamente pelos experimentos obrigatorios
├── Dockerfile            # Imagem de compilacao e execucao (Alpine Linux + GCC)
├── docker-compose.yml    # Orquestracao dos 11 conteineres em rede bridge isolada
├── Makefile              # Automacao de build e scripts de carga dos experimentos
└── README.md             # Documentacao do projeto
```

## 3. Dependências e Versões Fixadas

Conforme exigido nas regras de reprodutibilidade da entrega, o ambiente utiliza versões fixas:

| Componente | Versão | Papel no Sistema |
| :--- | :--- | :--- |
| Alpine Linux | 3.20 | Sistema operacional base dos contêineres |
| GCC (build-base) | 13.2+ | Compilador C com suporte a POSIX Threads e Sockets |
| Docker Engine | 24.0+ | Isolamento de processos e virtualização leve |
| Docker Compose | v2.20+ | Orquestração da topologia e injeção de variáveis |

## 4. Funcionamento do Protocolo e Ciclo de Vida

Cada pedido possui um identificador único composto por `(client_id, req_seq)`. Na inicialização do sistema, o gerenciador `node0` injeta um único *token* no anel lógico.

1. **Solicitação (`REQUEST`):** O cliente envia um pedido de escrita ao seu gerenciador local e aguarda bloqueado pela confirmação.
2. **Aquisição (`acquire` / `ACQUIRE`):** O gerenciador registra o pedido localmente e aguarda a chegada do *token* pela rede. O tempo de espera é medido localmente com relógio monotônico (`CLOCK_MONOTONIC`).
3. **Seção Crítica (`ENTER` / `BEGIN` / `END` / `EXIT`):** Ao reter o *token*, o nó notifica `BEGIN` ao Observador, aguarda confirmação de registro e executa um `usleep` aleatório entre 0,2 e 1,0 segundo simulando o acesso a $R$. Ao concluir o tempo, notifica `END` ao Observador.
4. **Liberação (`release` / `RELEASE` / `COMMITTED`):** O gerenciador despacha o *token* para o próximo vizinho do anel ($P_{(i+1) \pmod 5}$) e envia a mensagem `COMMITTED` de volta ao seu cliente.
5. **Novo Ciclo:** Na carga normal, o cliente aguarda entre 1 e 5 segundos aleatórios antes de emitir o próximo pedido.

## 5. Propriedades Garantidas e Análise Teórica

* **Exclusão Mútua:** Garantida pela existência de um único *token* em circulação no anel; apenas o detentor atual da ficha pode entrar na seção crítica.
* **Justiça (*Fairness*) e Ausência de *Starvation*:** O anel possui sentido único estrito e cada nó atende no máximo um pedido por passagem do *token*. Mesmo sob carga desigual, um nó é obrigado a repassar a ficha ao vizinho após usar $R$, limitando a espera máxima a $N-1$ acessos.
* **Progresso e Ausência de *Deadlock*:** Como não há espera circular por múltiplos detentores e a retenção da seção crítica é finita (0,2s a 1,0s), o *token* circula continuamente.
* **Custo de Mensagens:** Sob alta contenção, o protocolo exige exatamente $1$ transferência de *token* por acesso a $R$. Sob baixa disputa, o *token* circula livremente pelo anel gerando maior número médio de mensagens de protocolo por entrada.

## 6. Instalação e Execução dos Experimentos

Todos os experimentos obrigatórios descritos na Seção 7 do enunciado foram automatizados via `Makefile` e salvam seus respectivos registros na pasta `logs/`.

### 6.1. Compilar as Imagens
```bash
make build
```

### 6.2. Experimento 1 — Carga Normal
Executa os 5 clientes com pausas aleatórias de 1 a 5 segundos entre requisições e seção crítica de 0,2 a 1,0 segundo:
```bash
make run-normal
```

### 6.3. Experimento 2 — Alta Contenção (Teste de Estresse)
Remove a pausa entre pedidos dos clientes (`0 ms`), forçando disputa contínua e simultânea nos 5 gerenciadores:
```bash
make run-contention
```

### 6.4. Experimento 3 — Carga Desigual (Verificação de Justiça)
Configura o `client0` para disparar 30 requisições consecutivas sem pausa (`0 ms`), enquanto os demais clientes realizam 10 requisições espaçadas de 1 a 5 segundos:
```bash
make run-unequal
```

### 6.5. Experimento 4 — Sincronização Desabilitada (Prova de Instrumentação)
Executa a carga com `SYNC_ENABLED=0` para demonstrar que o processo Observador detecta acessos simultâneos e reporta violações quando o protocolo Token Ring está inativo:
```bash
make run-nosync
```

### 6.6. Modo Visual para Demonstração em Seminário
Exibe no terminal o estado do anel lógico em tempo real e a tabela consolidada do verificador:
```bash
make run-visual
```

### 6.7. Limpeza do Ambiente
Para encerrar os contêineres e limpar imagens ou logs gerados:
```bash
make stop
make clean
```

## 7. Métricas e Verificação Automática

Ao término de cada bateria, o contêiner `observer` imprime um relatório consolidado contendo:
* **Status da Exclusão Mútua:** Aprovação automática caso o total de sobreposições entre `BEGIN` e `END` seja zero.
* **Métricas Globais:** Total de pedidos concluídos, tempo total de execução, total de mensagens de protocolo (*token*) separadas das mensagens de instrumentação (`BEGIN`/`END`).
* **Métricas por Nó/Cliente:** Quantidade de acessos atendidos, espera mediana para aquisição em milissegundos e total de *tokens* repassados por participante.

## 8. Referências

1. GHOSH, Sukumar. *Distributed Systems: An Algorithmic Approach*. 2nd ed., Cap. 7, CRC Press.
2. LIMA, Joubert. *Enunciado do Trabalho Prático 2 (TP2) - Sincronização em Sistemas Distribuídos (BCC362)*. DECOM/UFOP, 2026.
