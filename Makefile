.PHONY: build run-normal run-contention run-unequal run-nosync run-visual stop clean

build:
	mkdir -p logs
	docker compose build

# Modo Limpo para Apresentacao: Mostra o Anel Visual e a Tabela Final com menos poluicao na tela
run-visual: build
	SYNC_ENABLED=1 VERBOSE_LOGS=0 C0_REQS=10 OTHER_REQS=10 \
	C0_MIN_SLEEP=0 C0_MAX_SLEEP=0 MIN_SLEEP_MS=0 MAX_SLEEP_MS=0 \
	docker compose up --exit-code-from observer
	docker compose down

# 1. Carga Normal (Logs completos + Tabela Final garantida)
run-normal: build
	SYNC_ENABLED=1 VERBOSE_LOGS=1 C0_REQS=10 OTHER_REQS=10 \
	C0_MIN_SLEEP=1000 C0_MAX_SLEEP=5000 MIN_SLEEP_MS=1000 MAX_SLEEP_MS=5000 \
	docker compose up --exit-code-from observer | tee logs/experimento_carga_normal.log
	docker compose down

# 2. Alta Contencao (Disputa maxima sem sleep entre pedidos)
run-contention: build
	SYNC_ENABLED=1 VERBOSE_LOGS=1 C0_REQS=15 OTHER_REQS=15 \
	C0_MIN_SLEEP=0 C0_MAX_SLEEP=0 MIN_SLEEP_MS=0 MAX_SLEEP_MS=0 \
	docker compose up --exit-code-from observer | tee logs/experimento_alta_contencao.log
	docker compose down

# 3. Carga Desigual (Cliente 0 faz 30 pedidos sem pausa vs 10 dos demais)
run-unequal: build
	SYNC_ENABLED=1 VERBOSE_LOGS=1 C0_REQS=30 OTHER_REQS=10 \
	C0_MIN_SLEEP=0 C0_MAX_SLEEP=0 MIN_SLEEP_MS=1000 MAX_SLEEP_MS=5000 \
	docker compose up --exit-code-from observer | tee logs/experimento_carga_desigual.log
	docker compose down

# 4. Sem Sincronizacao (Mostra colisoes em vermelho no anel)
run-nosync: build
	SYNC_ENABLED=0 VERBOSE_LOGS=0 C0_REQS=10 OTHER_REQS=10 \
	C0_MIN_SLEEP=0 C0_MAX_SLEEP=0 MIN_SLEEP_MS=0 MAX_SLEEP_MS=0 \
	docker compose up --exit-code-from observer | tee logs/experimento_sem_sincronizacao.log
	docker compose down

stop:
	docker compose down

clean:
	docker compose down --rmi local -v
	rm -rf logs/