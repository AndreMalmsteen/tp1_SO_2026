// Compilar: g++ -std=c++17 -pthread pc_v1_v2.cpp -o pc_v1_v2
// Uso: ./pc_v1_v2 CLIENTES CAIXAS OPS_POR_CLIENTE DELAY_CLIENTE_MS DELAY_CAIXA_MS [TAM_FILA] [SALDO_INICIAL]
//   Versao 1 (varios produtores, 1 consumidor):        .\pc_v1_v2 4 1 5 300 100
//   Versao 2 (varios produtores, varios consumidores): .\pc_v1_v2 4 3 5 300 100
#include "pc_v1_v2_cabecalho.hpp"

#include <chrono>
#include <cstdlib>
#include <random>
#include <thread>
#include <functional>


void cliente(FilaLimitada<Operacao>& fila, int id, int n, int delay_ms) {   // produtor
    std::random_device rd;
    std::mt19937 gen(rd() + id);
    std::uniform_int_distribution<int> tipo(0, 2), valor(10, 100);
    std::string nome = "Cliente " + std::to_string(id);
    for (int i = 0; i < n; i++) {
        Operacao op{static_cast<Operacao::Tipo>(tipo(gen)), id, valor(gen)};
        std::printf("%s: preparando %s\n", nome.c_str(), op.str().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        fila.inserir(op, nome);
    }
    std::printf("%s: finalizado\n", nome.c_str());
}

void caixa(FilaLimitada<Operacao>& fila, ContaBancaria& conta, int id,      // consumidor
           std::atomic<int>& restantes, int delay_ms) {
    std::string nome = "Caixa " + std::to_string(id);
    while (restantes.fetch_sub(1) > 0) {
        Operacao op = fila.retirar(nome);
        conta.processar(op, nome);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    std::printf("%s: finalizado\n", nome.c_str());
}

int main(int argc, char** argv) {
    if (argc < 6) {
        std::printf("Uso: %s CLIENTES CAIXAS OPS_POR_CLIENTE DELAY_CLIENTE_MS DELAY_CAIXA_MS [TAM_FILA] [SALDO_INICIAL]\n", argv[0]);
        return 1;
    }
    int nc = std::atoi(argv[1]), nx = std::atoi(argv[2]), ops = std::atoi(argv[3]);
    int dc = std::atoi(argv[4]), dx = std::atoi(argv[5]);
    int tam    = (argc > 6) ? std::atoi(argv[6]) : 5;
    int saldo0 = (argc > 7) ? std::atoi(argv[7]) : 100;
    if (nc < 1 || nx < 1 || ops < 1 || tam < 1 || dc < 0 || dx < 0) {
        std::printf("CLIENTES, CAIXAS, OPS e TAM devem ser >= 1 | DELAY_CLIENTE_MS e DELAY_CAIXA_MS devem ser >= 0 \n");
        return 1;
    }

    FilaLimitada<Operacao> fila(tam);
    ContaBancaria conta(saldo0);
    std::atomic<int> restantes(nc * ops);

    std::vector<std::thread> threads;
    for (int i = 1; i <= nc; i++)
        threads.emplace_back(cliente, std::ref(fila), i, ops, dc);
    for (int i = 1; i <= nx; i++)
        threads.emplace_back(caixa, std::ref(fila), std::ref(conta), i, std::ref(restantes), dx);

    for (auto& t : threads) t.join();

    std::printf("Todas as threads concluiram.\n");
    std::printf("Saldo final = %d | esperado = %d -> %s\n", conta.saldo(), conta.esperado(),
                conta.saldo() == conta.esperado() ? "CONSISTENTE" : "INCONSISTENTE");
    return 0;
}
