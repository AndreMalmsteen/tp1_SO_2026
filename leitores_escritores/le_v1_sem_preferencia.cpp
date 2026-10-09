// Leitores/Escritores - Versao 1: sem preferencia (semaforo so entre escritores; leitura suja pode ocorrer)
// Compilar: g++ -std=c++17 -pthread le_v1_sem_preferencia.cpp -o le_v1_sem_preferencia
// Usa o TAD ContaBancaria (banco_tad.hpp): Leitor = consulta de saldo, Escritor = deposito
#include "banco_tad.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

const double SLEEP_REGIAO_S = 1.0;   // pausa dentro da regiao critica (1 segundo)
const double MAX_ATRASO_S   = 3.0;   // limite maximo do atraso de chegada (3 segundos)
const int    SALDO_INICIAL  = 100;

void dormir(double seg) {
    seg = std::max(0.0, std::min(seg, MAX_ATRASO_S));
    std::this_thread::sleep_for(std::chrono::duration<double>(seg));
}

void leitor(ContaBancaria& conta, int id, double atraso_s) {
    std::string nome = "Leitor " + std::to_string(id);
    std::printf("%s: criado\n", nome.c_str());
    dormir(atraso_s);
    conta.consultarSemControle(nome, SLEEP_REGIAO_S);
    std::printf("%s: finalizado\n", nome.c_str());
}

void escritor(ContaBancaria& conta, int id, int valor, double atraso_s) {
    std::string nome = "Escritor " + std::to_string(id);
    std::printf("%s: criado\n", nome.c_str());
    dormir(atraso_s);
    conta.depositarComSemaforo(valor, nome, SLEEP_REGIAO_S);
    std::printf("%s: finalizado\n", nome.c_str());
}

int main() {
    int nl, ne;
    std::cout << "Quantidade de leitores: ";   std::cin >> nl;
    std::cout << "Quantidade de escritores: "; std::cin >> ne;

    // 1) Le TODA a configuracao antes de criar qualquer thread
    std::vector<int>    valores(ne);
    std::vector<double> atrasosE(ne), atrasosL(nl);
    for (int i = 0; i < ne; i++) {
        std::cout << "Escritor " << i << " - valor a depositar: ";                       std::cin >> valores[i];
        std::cout << "Escritor " << i << " - atraso de chegada (0 a 3 s, decimal ok): "; std::cin >> atrasosE[i];
    }
    for (int i = 0; i < nl; i++) {
        std::cout << "Leitor " << i << " - atraso de chegada (0 a 3 s, decimal ok): "; std::cin >> atrasosL[i];
    }

    ContaBancaria conta(SALDO_INICIAL);

    // 2) Cria todas as threads de uma vez (atrasos contados do mesmo instante)
    std::printf("\n===== INICIO DA SIMULACAO (saldo inicial = %d) =====\n", SALDO_INICIAL);
    std::vector<std::thread> threads;
    for (int i = 0; i < ne; i++)
        threads.emplace_back(escritor, std::ref(conta), i, valores[i], atrasosE[i]);
    for (int i = 0; i < nl; i++)
        threads.emplace_back(leitor, std::ref(conta), i, atrasosL[i]);

    for (auto& t : threads) t.join();

    std::printf("Todas as threads concluiram.\n");
    std::printf("Saldo final = %d | esperado = %d -> %s\n", conta.saldo(), conta.esperado(),
                conta.saldo() == conta.esperado() ? "CONSISTENTE" : "INCONSISTENTE");
    return 0;
}


// v1, leitura suja: printf "3\n1\n50\n0\n0.5\n1.5\n2.5\n" | ./le_v1_sem_preferencia
// v1, bloqueio entre escritores: printf "0\n2\n50\n0\n30\n0.5\n" | ./le_v1_sem_preferencia