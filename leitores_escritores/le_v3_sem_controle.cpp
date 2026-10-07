// Leitores/Escritores - Versao 3: SEM controle de concorrencia
// Compilar: g++ -std=c++17 -pthread le_v3_sem_controle.cpp -o le_v3_sem_controle
// Entrada: pedida pelo teclado (veja as perguntas do programa)
#include "conta_le_tad.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

const double SLEEP_REGIAO_S = 1.0;   // tempo dentro da regiao critica (1 segundo)
const double MAX_ATRASO_S   = 3.0;   // limite maximo de qualquer atraso (3 segundos)

void dormir(double seg) {
    seg = std::max(0.0, std::min(seg, MAX_ATRASO_S));
    std::this_thread::sleep_for(std::chrono::duration<double>(seg));
}

void leitor(ContaCompartilhada& conta, int id, double atraso_s) {
    std::string nome = "Leitor " + std::to_string(id);
    std::printf("%s: criado\n", nome.c_str());
    dormir(atraso_s);
    conta.ler(nome);
    std::printf("%s: finalizado\n", nome.c_str());
}

void escritor(ContaCompartilhada& conta, int id, int valor, double atraso_s) {
    std::string nome = "Escritor " + std::to_string(id);
    std::printf("%s: criado\n", nome.c_str());
    dormir(atraso_s);
    conta.escrever(valor, nome);
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
        std::cout << "Escritor " << i << " - valor a gravar: ";                          std::cin >> valores[i];
        std::cout << "Escritor " << i << " - atraso de chegada (0 a 3 s, decimal ok): "; std::cin >> atrasosE[i];
    }
    for (int i = 0; i < nl; i++) {
        std::cout << "Leitor " << i << " - atraso de chegada (0 a 3 s, decimal ok): "; std::cin >> atrasosL[i];
    }

    ContaCompartilhada conta(false, SLEEP_REGIAO_S);

    // 2) Cria todas as threads de uma vez (atrasos contados do mesmo instante)
    std::printf("\n===== INICIO DA SIMULACAO =====\n");
    std::vector<std::thread> threads;
    for (int i = 0; i < ne; i++)
        threads.emplace_back(escritor, std::ref(conta), i, valores[i], atrasosE[i]);
    for (int i = 0; i < nl; i++)
        threads.emplace_back(leitor, std::ref(conta), i, atrasosL[i]);

    for (auto& t : threads) t.join();

    std::printf("Todas as threads concluiram.\n");
    std::printf("Estado final: agencia=%d central=%d\n", conta.agencia(), conta.central());
    return 0;
}

// para compilar:
// g++ -std=c++17 -pthread le_v3_sem_controle.cpp -o le_v3

// v3, corrupção (mesma entrada do v1): printf "0\n2\n50\n0\n30\n0.5\n" | ./le_v3