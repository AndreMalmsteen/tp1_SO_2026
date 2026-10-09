#include "../produtores_consumidores/pc_v1_v2_cabecalho.hpp"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <random>
#include <thread>

static void dormir(int ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// TAD: conta bancaria com leitores (CONSULTA) e escritores (SAQUE/DEPOSITO).
// Versao 2: escritores com preferencia (2o problema de Courtois, Heymans e Parnas).
// Nao reaproveita ContaBancaria: o mutex interno dela serializaria ate os leitores.
class ContaLeitoresEscritores {
public:
    ContaLeitoresEscritores(int saldo_inicial, int tempo_leitura_ms, int tempo_escrita_ms)
        : saldo_(saldo_inicial), inicial_(saldo_inicial), depositado_(0), sacado_(0),
          tempoLeitura_(tempo_leitura_ms), tempoEscrita_(tempo_escrita_ms),
          leitoresAtivos_(0), escritoresPresentes_(0), leituras_(0), leiturasSujas_(0) {
        sem_init(&mutexLeitores_, 0, 1);    // protege leitoresAtivos_
        sem_init(&mutexEscritores_, 0, 1);  // protege escritoresPresentes_
        sem_init(&entrada_, 0, 1);          // so um leitor por vez disputa a porta
        sem_init(&porta_, 0, 1);            // fechada pelo 1o escritor, aberta pelo ultimo
        sem_init(&recurso_, 0, 1);          // acesso exclusivo a conta
    }
    ~ContaLeitoresEscritores() {
        sem_destroy(&mutexLeitores_);
        sem_destroy(&mutexEscritores_);
        sem_destroy(&entrada_);
        sem_destroy(&porta_);
        sem_destroy(&recurso_);
    }
    ContaLeitoresEscritores(const ContaLeitoresEscritores&) = delete;
    ContaLeitoresEscritores& operator=(const ContaLeitoresEscritores&) = delete;

    // Leitor: so entra quando nao ha escritor ativo nem esperando
    void consultar(const Operacao& op, const std::string& quem) {
        esperar(&entrada_, quem, "outro leitor esta na fila de entrada");
        esperar(&porta_, quem, "ha escritor ativo ou esperando (preferencia dos escritores)");
        sem_wait(&mutexLeitores_);
        if (++leitoresAtivos_ == 1)
            esperar(&recurso_, quem, "primeiro leitor aguardando o escritor ativo sair");
        sem_post(&mutexLeitores_);
        sem_post(&porta_);
        sem_post(&entrada_);

        // regiao critica: leitura em duas etapas; um escritor no meio causaria leitura suja
        std::printf("%s: ENTROU NA REGIAO CRITICA (leitores ativos: %d)\n",
                    quem.c_str(), leitoresAtivos_.load());
        int s = saldo_;
        dormir(tempoLeitura_ / 2);
        int dep = depositado_, sac = sacado_;
        dormir(tempoLeitura_ - tempoLeitura_ / 2);
        bool ok = (s == inicial_ + dep - sac);
        leituras_++;
        if (!ok) leiturasSujas_++;
        std::printf("%s: %s -> saldo = %d (depositado = %d, sacado = %d) %s\n",
                    quem.c_str(), op.str().c_str(), s, dep, sac,
                    ok ? "[consistente]" : "[*** LEITURA SUJA ***]");

        sem_wait(&mutexLeitores_);
        int restantes = --leitoresAtivos_;
        std::printf("%s: SAIU DA REGIAO CRITICA (leitores ativos: %d)\n", quem.c_str(), restantes);
        if (restantes == 0) sem_post(&recurso_);
        sem_post(&mutexLeitores_);
    }

    // Escritor: o 1o a chegar fecha a porta dos leitores; o ultimo a sair abre
    void atualizar(const Operacao& op, const std::string& quem) {
        sem_wait(&mutexEscritores_);
        if (++escritoresPresentes_ == 1)
            esperar(&porta_, quem, "fechando a porta dos leitores");
        sem_post(&mutexEscritores_);
        esperar(&recurso_, quem, "ha leitores ativos ou outro escritor na regiao critica");

        std::printf("%s: ENTROU NA REGIAO CRITICA (escritores presentes: %d)\n",
                    quem.c_str(), escritoresPresentes_.load());
        if (op.tipo == Operacao::DEPOSITO) {
            saldo_ += op.valor;                    // etapa 1
            dormir(tempoEscrita_ / 2);
            depositado_ += op.valor;               // etapa 2
            dormir(tempoEscrita_ - tempoEscrita_ / 2);
            std::printf("%s: %s -> OK, saldo = %d\n", quem.c_str(), op.str().c_str(), saldo_);
        } else if (op.tipo == Operacao::SAQUE && op.valor <= saldo_) {
            saldo_ -= op.valor;                    // etapa 1
            dormir(tempoEscrita_ / 2);
            sacado_ += op.valor;                   // etapa 2
            dormir(tempoEscrita_ - tempoEscrita_ / 2);
            std::printf("%s: %s -> OK, saldo = %d\n", quem.c_str(), op.str().c_str(), saldo_);
        } else {
            dormir(tempoEscrita_);
            std::printf("%s: %s -> RECUSADO (saldo insuficiente: %d)\n",
                        quem.c_str(), op.str().c_str(), saldo_);
        }
        sem_post(&recurso_);

        sem_wait(&mutexEscritores_);
        int restantes = --escritoresPresentes_;
        std::printf("%s: SAIU DA REGIAO CRITICA (escritores presentes: %d)\n",
                    quem.c_str(), restantes);
        if (restantes == 0) {
            std::printf("%s: ultimo escritor, LIBEROU a porta dos leitores\n", quem.c_str());
            sem_post(&porta_);
        }
        sem_post(&mutexEscritores_);
    }

    // Chamar so depois do join de todas as threads
    int saldo() const { return saldo_; }
    int esperado() const { return inicial_ + depositado_ - sacado_; }
    int leituras() const { return leituras_; }
    int leiturasSujas() const { return leiturasSujas_; }

private:
    // P que avisa quando a thread vai ser bloqueada
    void esperar(sem_t* s, const std::string& quem, const char* motivo) {
        if (sem_trywait(s) == -1 && errno == EAGAIN) {
            std::printf("%s: BLOQUEADO (%s)\n", quem.c_str(), motivo);
            sem_wait(s);
            std::printf("%s: DESBLOQUEADO\n", quem.c_str());
        }
    }

    int saldo_, inicial_, depositado_, sacado_;   // protegidos pelo protocolo acima
    int tempoLeitura_, tempoEscrita_;
    std::atomic<int> leitoresAtivos_, escritoresPresentes_, leituras_, leiturasSujas_;
    sem_t mutexLeitores_, mutexEscritores_, entrada_, porta_, recurso_;
};

void leitor(ContaLeitoresEscritores& conta, int id, int n, int delay_ms) {
    std::random_device rd;
    std::mt19937 gen(rd() + id);
    std::uniform_int_distribution<int> jitter(0, delay_ms / 2);
    std::string nome = "Leitor " + std::to_string(id);
    std::printf("%s: criado\n", nome.c_str());
    for (int i = 0; i < n; i++) {
        dormir(delay_ms + jitter(gen));
        Operacao op{Operacao::CONSULTA, id, 0};
        std::printf("%s: quer %s\n", nome.c_str(), op.str().c_str());
        conta.consultar(op, nome);
    }
    std::printf("%s: finalizado\n", nome.c_str());
}

void escritor(ContaLeitoresEscritores& conta, int id, int n, int delay_ms) {
    std::random_device rd;
    std::mt19937 gen(rd() + id);
    std::uniform_int_distribution<int> jitter(0, delay_ms / 2), moeda(0, 1), valor(10, 100);
    std::string nome = "Escritor " + std::to_string(id);
    std::printf("%s: criado\n", nome.c_str());
    for (int i = 0; i < n; i++) {
        dormir(delay_ms + jitter(gen));
        Operacao op{moeda(gen) ? Operacao::DEPOSITO : Operacao::SAQUE, id, valor(gen)};
        std::printf("%s: quer %s\n", nome.c_str(), op.str().c_str());
        conta.atualizar(op, nome);
    }
    std::printf("%s: finalizado\n", nome.c_str());
}

int main(int argc, char** argv) {
    if (argc < 7) {
        std::printf("Uso: %s LEITORES ESCRITORES OPS_LEITOR OPS_ESCRITOR DELAY_LEITOR_MS DELAY_ESCRITOR_MS "
                    "[TEMPO_LEITURA_MS] [TEMPO_ESCRITA_MS] [SALDO_INICIAL]\n", argv[0]);
        return 1;
    }
    int nl = std::atoi(argv[1]), ne = std::atoi(argv[2]);
    int opsl = std::atoi(argv[3]), opse = std::atoi(argv[4]);
    int dl = std::atoi(argv[5]), de = std::atoi(argv[6]);
    int tl     = (argc > 7) ? std::atoi(argv[7]) : 200;
    int te     = (argc > 8) ? std::atoi(argv[8]) : 400;
    int saldo0 = (argc > 9) ? std::atoi(argv[9]) : 100;
    if (nl < 0 || ne < 0 || nl + ne < 1 || opsl < 1 || opse < 1 || dl < 0 || de < 0 || tl < 0 || te < 0) {
        std::printf("Parametros invalidos (ao menos 1 thread; OPS >= 1; tempos >= 0)\n");
        return 1;
    }

    ContaLeitoresEscritores conta(saldo0, tl, te);

    std::vector<std::thread> threads;
    for (int i = 1; i <= nl; i++)
        threads.emplace_back(leitor, std::ref(conta), i, opsl, dl);
    for (int i = 1; i <= ne; i++)
        threads.emplace_back(escritor, std::ref(conta), i, opse, de);

    for (auto& t : threads) t.join();

    std::printf("Todas as threads concluiram.\n");
    std::printf("Leituras = %d | leituras sujas = %d\n", conta.leituras(), conta.leiturasSujas());
    std::printf("Saldo final = %d | esperado = %d -> %s\n", conta.saldo(), conta.esperado(),
                conta.saldo() == conta.esperado() ? "CONSISTENTE" : "INCONSISTENTE");
    return 0;
}
