#include "banco_tad.hpp"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <thread>

static void dormir(int ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Operacao + numero de sequencia unico (0 = posicao vazia), usado so para detectar
// perdas e duplicacoes. Operacao do TAD nao tem identificador proprio.
struct ItemFila {
    Operacao op{Operacao::CONSULTA, 0, 0};
    int seq = 0;
    std::string str() const { return "#" + std::to_string(seq) + " " + op.str(); }
};

// Fila circular SEM nenhum controle de concorrencia (sem semaforos, sem mutex).
// Os indices e o contador sao atomic apenas para nao haver comportamento indefinido
// em leituras/escritas isoladas; as atualizacoes continuam em duas etapas
// (load, janela de corrida, store), entao a condicao de corrida permanece.
// Os elementos de dados_ sao acessados sem nenhuma protecao.
class FilaSemControle {
public:
    FilaSemControle(int capacidade, int total, int janela_ms)
        : dados_(capacidade), consumido_(new std::atomic<int>[total + 1]()), janela_(janela_ms),
          entrada_(0), saida_(0), qtd_(0),
          sobrescritas_(0), duplicados_(0), vazios_(0), cheioIgnorado_(0) {}

    void inserir(const ItemFila& item, const std::string& quem) {
        const int cap = static_cast<int>(dados_.size());
        int pos = entrada_.load();       // le o indice...
        dormir(janela_);                 // ...outro produtor pode ler o MESMO indice

        if (qtd_.load() >= cap) {        // nao ha semaforo 'vazio_': produtor nao dorme
            cheioIgnorado_++;
            std::printf("%s: !!! fila CHEIA (%d/%d) e nada bloqueou o produtor\n",
                        quem.c_str(), qtd_.load(), cap);
        }
        if (dados_[pos].seq != 0) {
            sobrescritas_++;
            std::printf("%s: !!! SOBRESCRITA na posicao %d: %s ainda estava ali e foi PERDIDA\n",
                        quem.c_str(), pos, dados_[pos].str().c_str());
        }
        dados_[pos] = item;
        entrada_.store((pos + 1) % cap);

        int tmp = qtd_.load();           // qtd_++ em duas etapas: atualizacao perdida
        dormir(janela_ / 3);
        qtd_.store(tmp + 1);

        std::printf("%s: ENTROU NA FILA %s (posicao %d)\n%s\n",
                    quem.c_str(), item.str().c_str(), pos, estado().c_str());
    }

    ItemFila retirar(const std::string& quem) {
        const int cap = static_cast<int>(dados_.size());
        int pos = saida_.load();         // le o indice...
        dormir(janela_);                 // ...outro consumidor pode ler o MESMO indice
        ItemFila item = dados_[pos];

        if (item.seq == 0) {             // nao ha semaforo 'cheio_': consumidor nao dorme
            vazios_++;
            std::printf("%s: !!! posicao %d VAZIA (nao dormiu com a fila vazia / item ja levado)\n",
                        quem.c_str(), pos);
        } else {
            if (consumido_[item.seq].fetch_add(1) > 0) {
                duplicados_++;
                std::printf("%s: !!! CONSUMO DUPLICADO de %s\n", quem.c_str(), item.str().c_str());
            }
            dados_[pos].seq = 0;
            std::printf("%s: PEGOU %s (posicao %d)\n", quem.c_str(), item.str().c_str(), pos);
        }
        saida_.store((pos + 1) % cap);

        int tmp = qtd_.load();
        dormir(janela_ / 3);
        qtd_.store(tmp - 1);

        std::printf("%s\n", estado().c_str());
        return item;
    }

    // Chamar so depois do join de todas as threads
    void relatorio(int total) const {
        int distintas = 0, ocupadas = 0;
        for (int id = 1; id <= total; id++)
            if (consumido_[id].load() > 0) distintas++;
        for (const ItemFila& d : dados_)
            if (d.seq != 0) ocupadas++;
        std::printf("Operacoes produzidas ............: %d\n", total);
        std::printf("Operacoes consumidas (distintas) : %d\n", distintas);
        std::printf("Ainda na fila ...................: %d\n", ocupadas);
        std::printf("Operacoes PERDIDAS ..............: %d\n", total - distintas - ocupadas);
        std::printf("Eventos: sobrescritas = %d | consumos duplicados = %d | posicoes vazias consumidas = %d | "
                    "produtor com fila cheia = %d\n",
                    sobrescritas_.load(), duplicados_.load(), vazios_.load(), cheioIgnorado_.load());
        std::printf("Contador qtd_ = %d, posicoes realmente ocupadas = %d -> %s\n",
                    qtd_.load(), ocupadas, qtd_.load() == ocupadas ? "coincidiu (sorte)" : "INCONSISTENTE");
    }

private:
    std::string estado() const {
        std::string s = "   Fila [" + std::to_string(qtd_.load()) + "/" +
                        std::to_string(dados_.size()) + "]: ";
        for (const ItemFila& d : dados_)
            s += d.seq ? "[" + d.str() + "] " : "[__] ";
        return s + "(entrada=" + std::to_string(entrada_.load()) +
               ", saida=" + std::to_string(saida_.load()) + ")";
    }

    std::vector<ItemFila> dados_;
    std::unique_ptr<std::atomic<int>[]> consumido_;   // consumido_[seq] = vezes que foi retirada
    int janela_;
    std::atomic<int> entrada_, saida_, qtd_;
    std::atomic<int> sobrescritas_, duplicados_, vazios_, cheioIgnorado_;
};

// Conta bancaria SEM exclusao mutua: saldo = saldo +/- valor em duas etapas
// (a janela de corrida permite que dois caixas leiam o mesmo saldo).
class ContaSemControle {
public:
    ContaSemControle(int saldo_inicial, int janela_ms)
        : saldo_(saldo_inicial), inicial_(saldo_inicial), depositado_(0), sacado_(0),
          janela_(janela_ms) {}

    void processar(const Operacao& op, const std::string& caixa) {
        switch (op.tipo) {
        case Operacao::CONSULTA:
            std::printf("%s: %s -> saldo = %d\n", caixa.c_str(), op.str().c_str(), saldo_.load());
            break;
        case Operacao::DEPOSITO: {
            int s = saldo_.load();
            dormir(janela_ / 3);
            saldo_.store(s + op.valor);
            depositado_ += op.valor;
            std::printf("%s: %s -> OK, saldo = %d\n", caixa.c_str(), op.str().c_str(), saldo_.load());
            break;
        }
        case Operacao::SAQUE:
            if (op.valor <= saldo_.load()) {
                int s = saldo_.load();
                dormir(janela_ / 3);
                saldo_.store(s - op.valor);
                sacado_ += op.valor;
                std::printf("%s: %s -> OK, saldo = %d\n", caixa.c_str(), op.str().c_str(), saldo_.load());
            } else {
                std::printf("%s: %s -> RECUSADO (saldo insuficiente: %d)\n",
                            caixa.c_str(), op.str().c_str(), saldo_.load());
            }
            break;
        }
    }

    // Chamar so depois do join de todas as threads
    int saldo() const { return saldo_.load(); }
    int esperado() const { return inicial_ + depositado_ - sacado_; }

private:
    std::atomic<int> saldo_;
    int inicial_;
    std::atomic<int> depositado_, sacado_;   // contabilidade correta: so o saldo sofre a corrida
    int janela_;
};

void cliente(FilaSemControle& fila, int id, int n, int delay_ms) {   // produtor
    std::random_device rd;
    std::mt19937 gen(rd() + id);
    std::uniform_int_distribution<int> tipo(0, 2), valor(10, 100);
    std::string nome = "Cliente " + std::to_string(id);
    for (int i = 0; i < n; i++) {
        ItemFila item;
        item.op = Operacao{static_cast<Operacao::Tipo>(tipo(gen)), id, valor(gen)};
        item.seq = (id - 1) * n + i + 1;                              // unico e deterministico
        std::printf("%s: preparando %s\n", nome.c_str(), item.str().c_str());
        dormir(delay_ms);
        fila.inserir(item, nome);
    }
    std::printf("%s: finalizado\n", nome.c_str());
}

void caixa(FilaSemControle& fila, ContaSemControle& conta, int id,    // consumidor
           std::atomic<int>& restantes, int delay_ms) {
    std::string nome = "Caixa " + std::to_string(id);
    while (restantes.fetch_sub(1) > 0) {
        ItemFila item = fila.retirar(nome);
        if (item.seq != 0) conta.processar(item.op, nome);
        dormir(delay_ms);
    }
    std::printf("%s: finalizado\n", nome.c_str());
}

int main(int argc, char** argv) {
    if (argc < 6) {
        std::printf("Uso: %s CLIENTES CAIXAS OPS_POR_CLIENTE DELAY_CLIENTE_MS DELAY_CAIXA_MS "
                    "[TAM_FILA] [SALDO_INICIAL] [JANELA_MS]\n", argv[0]);
        return 1;
    }
    int nc = std::atoi(argv[1]), nx = std::atoi(argv[2]), ops = std::atoi(argv[3]);
    int dc = std::atoi(argv[4]), dx = std::atoi(argv[5]);
    int tam    = (argc > 6) ? std::atoi(argv[6]) : 5;
    int saldo0 = (argc > 7) ? std::atoi(argv[7]) : 100;
    int janela = (argc > 8) ? std::atoi(argv[8]) : 30;
    if (nc < 1 || nx < 1 || ops < 1 || tam < 1 || dc < 0 || dx < 0 || janela < 0) {
        std::printf("CLIENTES, CAIXAS, OPS e TAM devem ser >= 1; tempos >= 0\n");
        return 1;
    }

    const int total = nc * ops;
    FilaSemControle fila(tam, total, janela);
    ContaSemControle conta(saldo0, janela);
    std::atomic<int> restantes(total);

    std::vector<std::thread> threads;
    for (int i = 1; i <= nc; i++)
        threads.emplace_back(cliente, std::ref(fila), i, ops, dc);
    for (int i = 1; i <= nx; i++)
        threads.emplace_back(caixa, std::ref(fila), std::ref(conta), i, std::ref(restantes), dx);

    for (auto& t : threads) t.join();

    std::printf("Todas as threads concluiram.\n");
    fila.relatorio(total);
    std::printf("Saldo final = %d | esperado = %d -> %s\n", conta.saldo(), conta.esperado(),
                conta.saldo() == conta.esperado() ? "CONSISTENTE" : "INCONSISTENTE");
    return 0;
}