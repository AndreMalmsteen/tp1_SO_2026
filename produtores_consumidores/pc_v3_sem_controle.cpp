// Versao 3 (produtores/consumidores SEM controle de concorrencia): demonstra os problemas.
// Compilar: g++ -std=c++17 -pthread pc_v3_sem_controle.cpp -o banco_v3
// Uso: ./banco_v3 CLIENTES CAIXAS OPS_POR_CLIENTE DELAY_CLIENTE_MS DELAY_CAIXA_MS [TAM_FILA] [SALDO_INICIAL] [JANELA_MS]
//   Exemplo: ./banco_v3 4 2 5 300 400 5 1000 30
// Depende de banco_tad.hpp (so a struct Operacao): entregar os dois arquivos juntos.
//
// Nao ha semaforo nem mutex protegendo a fila ou a conta (printf ja serializa cada linha impressa).
// Os "!!!" marcam, com o tempo desde o inicio, onde a versao correta (v1/v2) bloquearia ou
// protegeria a regiao critica.
#include "pc_v1_v2_cabecalho.hpp"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <thread>

static const auto inicio = std::chrono::steady_clock::now();

// milissegundos desde o inicio do programa (usado nas linhas "!!!" para mostrar o "quando")
static long agora_ms() {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - inicio).count());
}

static void dormir(int ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Cada thread tem um ritmo proprio (fator fixo, 0.6 a 1.4) e um jitter de ate 25% a cada
// operacao, para as threads nao andarem em sincronia.
static double sortear_fator(std::mt19937& gen) {
    return std::uniform_real_distribution<double>(0.6, 1.4)(gen);
}
static int atraso(int base_ms, double fator, std::mt19937& gen) {
    double jitter = std::uniform_real_distribution<double>(0.0, 0.25)(gen);
    return static_cast<int>(base_ms * fator * (1.0 + jitter));
}

// Operacao + numero de sequencia unico (0 = posicao vazia), usado so para detectar
// perdas e duplicacoes. Operacao do TAD nao tem identificador proprio.
struct ItemFila {
    Operacao op{Operacao::CONSULTA, 0, 0};
    int seq = 0;
    std::string str() const { return "#" + std::to_string(seq) + " " + op.str(); }
};

// Fila circular SEM nenhum controle de concorrencia (sem semaforos, sem mutex).
//
// - JANELA_MS (sleep entre ler um indice e usa-lo) e so um AMPLIFICADOR da corrida: sem ela a
//   corrida continuaria existindo, mas seria rara e dificil de observar.
// - entrada_, saida_ e qtd_ sao atomic apenas para evitar comportamento indefinido nas leituras
//   e escritas isoladas; as atualizacoes continuam em duas etapas (load, janela, store), entao a
//   atualizacao perdida permanece.
// - Os elementos de dados_ sao lidos e escritos SEM nenhuma protecao. Isso e uma corrida de dados
//   (comportamento indefinido em C++) e uma leitura pode ver um ItemFila pela metade. E
//   intencional: faz parte do que a versao sem controle quer mostrar.
class FilaSemControle {
public:
    FilaSemControle(int capacidade, int total, int janela_ms)
        : dados_(capacidade), consumido_(new std::atomic<int>[total + 1]()), janela_(janela_ms),
          entrada_(0), saida_(0), qtd_(0),
          sobrescritas_(0), reaproveitadas_(0), duplicados_(0), vazios_(0),
          cheioIgnorado_(0), reocupadas_(0) {}

    void inserir(const ItemFila& item, const std::string& quem) {
        const int cap = static_cast<int>(dados_.size());
        int pos = entrada_.load();       // le o indice...
        dormir(janela_);                 // ...outro produtor pode ler o MESMO indice

        // Aqui a versao correta dormiria no semaforo "vazio_" (fila cheia).
        if (qtd_.load() >= cap) {
            cheioIgnorado_++;
            std::printf("[t=%ldms] %s: !!! fila CHEIA (%d/%d) e nada bloqueou o produtor "
                        "(aqui ele deveria DORMIR)\n", agora_ms(), quem.c_str(), qtd_.load(), cap);
        }

        ItemFila antigo = dados_[pos];
        if (antigo.seq != 0) {
            if (consumido_[antigo.seq].load() > 0) {
                // o consumidor ja levou o item, mas ainda nao limpou a posicao: nao e perda
                reaproveitadas_++;
                std::printf("[t=%ldms] %s: !!! posicao %d reaproveitada: %s ja havia sido consumido "
                            "e ainda nao tinha sido limpo (nao e perda)\n",
                            agora_ms(), quem.c_str(), pos, antigo.str().c_str());
            } else {
                sobrescritas_++;
                std::printf("[t=%ldms] %s: !!! SOBRESCRITA na posicao %d: %s ainda estava ali e foi PERDIDO\n",
                            agora_ms(), quem.c_str(), pos, antigo.str().c_str());
            }
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

        if (item.seq == 0) {
            // Aqui a versao correta dormiria no semaforo "cheio_" (fila vazia).
            vazios_++;
            std::printf("[t=%ldms] %s: !!! posicao %d VAZIA (aqui o consumidor deveria DORMIR; "
                        "ou o item ja foi levado)\n", agora_ms(), quem.c_str(), pos);
        } else {
            if (consumido_[item.seq].fetch_add(1) > 0) {
                duplicados_++;
                std::printf("[t=%ldms] %s: !!! CONSUMO DUPLICADO de %s\n",
                            agora_ms(), quem.c_str(), item.str().c_str());
            }
            // so limpa a posicao se ela ainda guarda o mesmo item; do contrario outro produtor
            // ja a reocupou e apagar levaria o item novo embora sem aviso
            if (dados_[pos].seq == item.seq) {
                dados_[pos].seq = 0;
            } else {
                reocupadas_++;
                std::printf("[t=%ldms] %s: !!! posicao %d ja foi reocupada por %s: item novo preservado\n",
                            agora_ms(), quem.c_str(), pos, dados_[pos].str().c_str());
            }
        }
        saida_.store((pos + 1) % cap);

        int tmp = qtd_.load();
        dormir(janela_ / 3);
        qtd_.store(tmp - 1);

        // uma unica chamada: "PEGOU" e o estado saem juntos, sem linhas de outras threads no meio
        if (item.seq != 0)
            std::printf("%s: PEGOU %s (posicao %d)\n%s\n",
                        quem.c_str(), item.str().c_str(), pos, estado().c_str());
        else
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
        std::printf("Eventos: sobrescritas = %d | posicoes vazias consumidas = %d | consumos duplicados = %d | "
                    "produtor com fila cheia = %d\n",
                    sobrescritas_.load(), vazios_.load(), duplicados_.load(), cheioIgnorado_.load());
        std::printf("Outros: posicoes reaproveitadas apos consumo (nao e perda) = %d | "
                    "posicoes reocupadas antes da limpeza = %d\n",
                    reaproveitadas_.load(), reocupadas_.load());
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
    std::atomic<int> sobrescritas_, reaproveitadas_, duplicados_, vazios_, cheioIgnorado_, reocupadas_;
};

// Conta bancaria SEM exclusao mutua: saldo = saldo +/- valor em duas etapas
// (a janela permite que dois caixas leiam o mesmo saldo). Esta corrida e probabilistica:
// so aparece quando dois caixas processam ao mesmo tempo, e poucas operacoes chegam a conta
// porque muitas leituras da fila acertam posicoes vazias.
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
            if (s != saldo_.load())
                std::printf("[t=%ldms] %s: !!! saldo mudou durante o deposito (lido %d, agora %d): "
                            "atualizacao sera PERDIDA\n", agora_ms(), caixa.c_str(), s, saldo_.load());
            saldo_.store(s + op.valor);
            depositado_ += op.valor;
            std::printf("%s: %s -> OK, saldo = %d\n", caixa.c_str(), op.str().c_str(), saldo_.load());
            break;
        }
        case Operacao::SAQUE:
            if (op.valor <= saldo_.load()) {
                int s = saldo_.load();
                dormir(janela_ / 3);
                if (s != saldo_.load())
                    std::printf("[t=%ldms] %s: !!! saldo mudou durante o saque (lido %d, agora %d): "
                                "atualizacao sera PERDIDA\n", agora_ms(), caixa.c_str(), s, saldo_.load());
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
    double fator = sortear_fator(gen);
    std::string nome = "Cliente " + std::to_string(id);
    for (int i = 0; i < n; i++) {
        ItemFila item;
        item.op = Operacao{static_cast<Operacao::Tipo>(tipo(gen)), id, valor(gen)};
        item.seq = (id - 1) * n + i + 1;                              // unico e deterministico
        std::printf("%s: preparando %s\n", nome.c_str(), item.str().c_str());
        dormir(atraso(delay_ms, fator, gen));
        fila.inserir(item, nome);
    }
    std::printf("%s: finalizado\n", nome.c_str());
}

void caixa(FilaSemControle& fila, ContaSemControle& conta, int id,    // consumidor
           std::atomic<int>& restantes, int delay_ms) {
    std::random_device rd;
    std::mt19937 gen(rd() + 1000 + id);
    double fator = sortear_fator(gen);
    std::string nome = "Caixa " + std::to_string(id);
    // restantes conta TENTATIVAS de retirada, nao sucessos: como a fila perde itens, se so os
    // sucessos zerassem o contador, os itens perdidos nunca o zerariam e o programa nao terminaria
    while (restantes.fetch_sub(1) > 0) {
        dormir(atraso(delay_ms, fator, gen));    // o caixa espera um pouco antes de atender
        ItemFila item = fila.retirar(nome);
        if (item.seq != 0) conta.processar(item.op, nome);
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
