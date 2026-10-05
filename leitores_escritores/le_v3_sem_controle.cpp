// Leitores/Escritores - Versao 3: SEM controle de concorrencia
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <string>
#include <algorithm>
using namespace std;

const double SLEEP_REGIAO_S = 1.0;   // tempo dentro da regiao critica (1 segundo)
const double MAX_ATRASO_S   = 3.0;   // limite maximo de qualquer atraso (3 segundos)

// Dado compartilhado: os dois campos DEVEM ser sempre iguais
struct Conta {
    int saldo_agencia = 0;
    int saldo_central = 0;
};
Conta conta;

mutex mtx_print;  // so para a saida nao embaralhar (NAO protege o dado)

void registrar(const string& msg) {
    lock_guard<mutex> lk(mtx_print);
    cout << msg << endl;
}

void dormir(double segundos) {
    segundos = max(0.0, min(segundos, MAX_ATRASO_S));   // limita a 3 s
    this_thread::sleep_for(chrono::duration<double>(segundos));
}

void leitor(int id, double atraso_s) {
    string nome = "[Leitor " + to_string(id) + "] ";
    registrar(nome + "criado");
    dormir(atraso_s);

    registrar(nome + "ENTROU na regiao critica");
    int a = conta.saldo_agencia;
    int c = conta.saldo_central;
    string msg = nome + "leu agencia=" + to_string(a) + " central=" + to_string(c);
    if (a != c) msg += "  <-- LEITURA SUJA!";
    registrar(msg);

    dormir(SLEEP_REGIAO_S);
    registrar(nome + "SAIU da regiao critica e finalizou");
}

void escritor(int id, int valor, double atraso_s) {
    string nome = "[Escritor " + to_string(id) + "] ";
    registrar(nome + "criado");
    dormir(atraso_s);

    registrar(nome + "ENTROU na regiao critica (sem nenhum controle)");
    conta.saldo_agencia = valor;              // passo 1
    registrar(nome + "gravou agencia=" + to_string(valor));

    dormir(SLEEP_REGIAO_S);                   // janela de inconsistencia

    conta.saldo_central = valor;              // passo 2
    registrar(nome + "gravou central=" + to_string(valor));
    int a = conta.saldo_agencia, c = conta.saldo_central;
    string fim = nome + "SAIU da regiao critica e finalizou. Estado: agencia="
        + to_string(a) + " central=" + to_string(c);
    if (a != valor || c != valor) fim += "  <-- DADO CORROMPIDO (diferente do que eu gravei)";
    registrar(fim);
}

int main() {
    int n_leitores, n_escritores;
    cout << "Quantidade de leitores: ";   cin >> n_leitores;
    cout << "Quantidade de escritores: "; cin >> n_escritores;

    // 1) Primeiro le TODA a configuracao (as threads ainda nao existem)
    vector<int>    valores(n_escritores);
    vector<double> atrasos_e(n_escritores), atrasos_l(n_leitores);

    for (int i = 0; i < n_escritores; i++) {
        cout << "Escritor " << i << " - valor a gravar: ";                        cin >> valores[i];
        cout << "Escritor " << i << " - atraso de chegada (0 a 3 s, decimal ok): "; cin >> atrasos_e[i];
    }
    for (int i = 0; i < n_leitores; i++) {
        cout << "Leitor " << i << " - atraso de chegada (0 a 3 s, decimal ok): "; cin >> atrasos_l[i];
    }

    // 2) Depois cria todas as threads de uma vez, para os atrasos
    //    serem contados a partir do mesmo instante
    cout << "\n===== INICIO DA SIMULACAO =====" << endl;
    vector<thread> threads;
    for (int i = 0; i < n_escritores; i++)
        threads.emplace_back(escritor, i, valores[i], atrasos_e[i]);
    for (int i = 0; i < n_leitores; i++)
        threads.emplace_back(leitor, i, atrasos_l[i]);

    for (auto& t : threads) t.join();

    cout << "Estado final: agencia=" << conta.saldo_agencia << " central=" << conta.saldo_central << endl;
    return 0;
}

// para compilar:
// g++ -std=c++17 -pthread le_v3_sem_controle.cpp -o le_v3