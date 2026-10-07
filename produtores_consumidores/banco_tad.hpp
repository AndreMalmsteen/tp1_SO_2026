#pragma once
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <semaphore.h>
#include <string>
#include <vector>

// Item que trafega pela fila
struct Operacao {
    enum Tipo { CONSULTA, SAQUE, DEPOSITO };
    Tipo tipo;
    int idCliente;
    int valor;   // ignorado em CONSULTA

    std::string str() const {
        const char* nomes[] = {"CONSULTA", "SAQUE", "DEPOSITO"};
        std::string s = "C " + std::to_string(idCliente) + ":" + nomes[tipo];
        if (tipo != CONSULTA) s += " " + std::to_string(valor);
        return s;
    }
};

// TAD: fila circular limitada (buffer). Requer T::str().
template <typename T>
class FilaLimitada {
public:
    explicit FilaLimitada(int capacidade)
        : dados_(capacidade), entrada_(0), saida_(0), qtd_(0) {
        sem_init(&vazio_, 0, capacidade);
        sem_init(&cheio_, 0, 0);
        sem_init(&mutex_, 0, 1);
    }
    ~FilaLimitada() {
        sem_destroy(&vazio_);
        sem_destroy(&cheio_);
        sem_destroy(&mutex_);
    }
    FilaLimitada(const FilaLimitada&) = delete;
    FilaLimitada& operator=(const FilaLimitada&) = delete;

    void inserir(const T& item, const std::string& quem) {
        if (sem_trywait(&vazio_) == -1 && errno == EAGAIN) {
            std::printf("%s: DORMINDO (fila cheia)\n", quem.c_str());
            sem_wait(&vazio_);
            std::printf("%s: ACORDOU\n", quem.c_str());
        }
        sem_wait(&mutex_);
        dados_[entrada_] = item;
        entrada_ = (entrada_ + 1) % dados_.size();
        qtd_++;
        // um unico printf: a linha sai inteira, sem misturar com outras threads
        std::printf("%s: ENTROU NA FILA %s\n%s\n",
                    quem.c_str(), item.str().c_str(), estado().c_str());
        sem_post(&mutex_);
        sem_post(&cheio_);
    }

    T retirar(const std::string& quem) {
        if (sem_trywait(&cheio_) == -1 && errno == EAGAIN) {
            std::printf("%s: DORMINDO (fila vazia)\n", quem.c_str());
            sem_wait(&cheio_);
            std::printf("%s: ACORDOU\n", quem.c_str());
        }
        sem_wait(&mutex_);
        T item = dados_[saida_];
        saida_ = (saida_ + 1) % dados_.size();
        qtd_--;
        std::printf("%s: PEGOU %s\n%s\n",
                    quem.c_str(), item.str().c_str(), estado().c_str());
        sem_post(&mutex_);
        sem_post(&vazio_);
        return item;
    }

private:
    // Chamar somente com mutex_ adquirido
    std::string estado() const {
        std::string s = "   Fila [" + std::to_string(qtd_) + "/" +
                        std::to_string(dados_.size()) + "]: ";
        if (qtd_ == 0) s += "(vazia)";
        for (int i = 0; i < qtd_; i++)
            s += "[" + dados_[(saida_ + i) % dados_.size()].str() + "] ";
        return s;
    }

    std::vector<T> dados_;
    size_t entrada_, saida_;
    int qtd_;
    sem_t vazio_, cheio_, mutex_;
};

// TAD: conta bancaria (segunda regiao critica)
class ContaBancaria {
public:
    explicit ContaBancaria(int saldo_inicial)
        : saldo_(saldo_inicial), inicial_(saldo_inicial), depositado_(0), sacado_(0) {
        sem_init(&mutex_, 0, 1);
    }
    ~ContaBancaria() { sem_destroy(&mutex_); }
    ContaBancaria(const ContaBancaria&) = delete;
    ContaBancaria& operator=(const ContaBancaria&) = delete;

    void processar(const Operacao& op, const std::string& caixa) {
        sem_wait(&mutex_);
        switch (op.tipo) {
        case Operacao::CONSULTA:
            std::printf("%s: %s -> saldo = %d\n", caixa.c_str(), op.str().c_str(), saldo_);
            break;
        case Operacao::DEPOSITO:
            saldo_ += op.valor;
            depositado_ += op.valor;
            std::printf("%s: %s -> OK, saldo = %d\n", caixa.c_str(), op.str().c_str(), saldo_);
            break;
        case Operacao::SAQUE:
            if (op.valor <= saldo_) {
                saldo_ -= op.valor;
                sacado_ += op.valor;
                std::printf("%s: %s -> OK, saldo = %d\n", caixa.c_str(), op.str().c_str(), saldo_);
            } else {
                std::printf("%s: %s -> RECUSADO (saldo insuficiente: %d)\n",
                            caixa.c_str(), op.str().c_str(), saldo_);
            }
            break;
        }
        sem_post(&mutex_);
    }

    // Chamar so depois do join de todas as threads
    int saldo() const { return saldo_; }
    int esperado() const { return inicial_ + depositado_ - sacado_; }

private:
    int saldo_;
    int inicial_;
    std::atomic<int> depositado_;
    std::atomic<int> sacado_;
    sem_t mutex_;
};