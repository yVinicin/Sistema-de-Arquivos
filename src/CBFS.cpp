#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <iomanip>

using namespace std;

// --- Configurações ---
const int TAM_SETOR = 512;                     // Tamanho do setor conforme especificado (512 bytes)
const uint16_t ASSINATURA_BOOT = 0x7777;       // Assinatura de boot esperada no Boot Record
const string NOME_SISTEMA = "CBFS";            // Nome do sistema de arquivos (CBFS)

// Estrutura do Setor de Boot (conforme Tabela 1 do documento)
#pragma pack(push, 1)                          // Desabilita alinhamento de bytes para garantir estrutura compacta
struct BootRecord {
    uint16_t bytes_por_setor;                  // Bytes por setor (512)
    uint16_t setores_reservados;               // Setores reservados (apenas o boot = 1)
    uint16_t max_entradas_root;                // Máximo de entradas no diretório raiz (padrão 128)
    uint16_t tam_root_setores;                 // Tamanho do diretório raiz em setores (8 setores)
    uint32_t total_setores;                    // Total de setores do disco (definido na formatação)
    uint32_t inicio_bitmap;                    // Setor inicial do bitmap (logo após o boot = 1)
    uint32_t tam_bitmap_setores;               // Tamanho do bitmap em setores (calculado como N/4096)
    uint32_t inicio_root;                      // Início do diretório raiz (após o bitmap)
    uint32_t inicio_dados;                     // Início da área de dados (após o diretório raiz)
    uint8_t  padding_gap[4];                   // Padding reservado (preenchido com zeros)
    char     nome_sistema[4];                  // Nome do sistema ("CBFS")
    uint16_t assinatura;                       // Assinatura de boot (0x7777)
    uint8_t  padding_final[474];               // Padding final para completar 512 bytes
};

// Estrutura da Entrada de Diretório (32 bytes conforme especificação)
struct EntradaDir {
    char     nome[16];                         // Nome do arquivo (16 bytes, alinhado à esquerda com espaços)
    char     ext[3];                           // Extensão do arquivo (3 bytes, ex: "TXT")
    uint8_t  atributo;                         // Atributo do arquivo (ver Tabela de Atributos)
    uint32_t tamanho_bytes;                    // Tamanho real do arquivo em bytes
    uint32_t primeiro_setor;                   // Setor LBA inicial do arquivo (alocação contígua)
    uint32_t num_setores;                      // Número de setores alocados para o arquivo
};
#pragma pack(pop)                              // Restaura alinhamento padrão

// Atributos de arquivo conforme Tabela 3 do documento
const uint8_t ATTR_LIVRE = 0x00;               // Entrada livre (fim da lista)
const uint8_t ATTR_VALIDO = 0x10;              // Arquivo válido e íntegro
const uint8_t ATTR_EXCLUIDO = 0x20;            // Arquivo excluído (espaço liberado, nome persiste)
const uint8_t ATTR_MODIFICADO = 0x30;          // Arquivo modificado (archive bit)

// Classe principal do sistema de arquivos CBFS
class CBFS {
    string dispositivo;                        // Caminho do arquivo de disco (.img)
    BootRecord br;                             // Estrutura do Boot Record carregada na memória

    // Abre o dispositivo (arquivo de disco) no modo especificado
    fstream abrir(ios::openmode modo) {
        fstream f(dispositivo, modo | ios::binary);
        if (!f.is_open()) throw runtime_error("Erro ao abrir o dispositivo: " + dispositivo);
        return f;
    }

    // Carrega os metadados (Boot Record) do disco e valida a assinatura
    void carregar_metadados() {
        fstream f = abrir(ios::in);
        f.read((char*)&br, sizeof(BootRecord));
        if (br.assinatura != ASSINATURA_BOOT) 
            throw runtime_error("Disco nao formatado como CBFS (Assinatura invalida).");
    }

    // Remove espaços à direita de uma string (usado para nomes e extensões)
    string limpar_string(string s) {
        size_t last = s.find_last_not_of(' ');
        if (last == string::npos) return "";
        return s.substr(0, last + 1);
    }

    // Define ou limpa um bit no Bitmap de Espaço Livre para um setor específico
    void set_bitmap(fstream &f, uint32_t setor, bool ocupado) {
        long offset = (long)br.inicio_bitmap * TAM_SETOR + (setor / 8);
        int bit = setor % 8;
        
        char byte;
        f.seekg(offset);
        f.read(&byte, 1);
        
        if (ocupado) byte |= (1 << bit);       // Marca como ocupado (bit = 1)
        else byte &= ~(1 << bit);              // Marca como livre (bit = 0)
        
        f.seekp(offset);
        f.write(&byte, 1);
    }

    // Busca um bloco contíguo de setores livres na região de dados
    uint32_t buscar_espaco_livre(fstream &f, uint32_t qtd_setores) {
        // Lê todo o bitmap para memória
        vector<uint8_t> bmp(br.tam_bitmap_setores * TAM_SETOR);
        f.seekg((long)br.inicio_bitmap * TAM_SETOR);
        f.read((char*)bmp.data(), bmp.size());

        uint32_t contador = 0;
        uint32_t inicio_candidato = 0;

        // Procura por um intervalo contíguo de setores livres
        for(uint32_t i = br.inicio_dados; i < br.total_setores; i++) {
            bool ocupado = (bmp[i/8] >> (i%8)) & 1;
            
            if (!ocupado) {
                if (contador == 0) inicio_candidato = i;
                contador++;
                if (contador == qtd_setores) return inicio_candidato;
            } else {
                contador = 0;                   // Reset se encontrar um setor ocupado
            }
        }

        // Se não encontrou, calcula o total de setores livres para diagnóstico
        uint32_t total_livres = 0;
        for(uint32_t i = br.inicio_dados; i < br.total_setores; i++) {
            if (!((bmp[i/8] >> (i%8)) & 1)) total_livres++;
        }

        // Lança erro específico: disco cheio ou fragmentado
        if (total_livres < qtd_setores) {
            throw runtime_error("Erro: Disco cheio! Espaco livre insuficiente.");
        } else {
            throw runtime_error("Erro: Fragmentacao! Ha espaco livre, mas nao contiguo.");
        }
    }

public:
    CBFS(string d) : dispositivo(d) {}         // Construtor: recebe o caminho do arquivo de disco

    // Comando: formatar - cria um novo disco CBFS com as estruturas iniciais
    void formatar(int total_setores) {
        memset(&br, 0, sizeof(br));            // Zera a estrutura do Boot Record
        br.bytes_por_setor = TAM_SETOR;
        br.setores_reservados = 1; 
        br.max_entradas_root = 128;
        br.tam_root_setores = 8;
        br.total_setores = total_setores;
        
        br.inicio_bitmap = 1;
        br.tam_bitmap_setores = (total_setores + 4095) / 4096; // Arredonda para cima
        if (br.tam_bitmap_setores == 0) br.tam_bitmap_setores = 1;
        
        br.inicio_root = br.inicio_bitmap + br.tam_bitmap_setores;
        br.inicio_dados = br.inicio_root + br.tam_root_setores;
        
        strncpy(br.nome_sistema, NOME_SISTEMA.c_str(), 4);
        br.assinatura = ASSINATURA_BOOT;

        // Cria o arquivo de disco e escreve o Boot Record
        ofstream f(dispositivo, ios::binary);
        if (!f) throw runtime_error("Falha ao criar arquivo de disco.");
        
        f.write((char*)&br, sizeof(br));
        // Estende o arquivo para o tamanho total (setores * 512)
        f.seekp((long)total_setores * TAM_SETOR - 1);
        char z = 0; f.write(&z, 1);
        f.close();

        // Inicializa o Bitmap com zeros e marca os setores de metadados como ocupados
        fstream fs = abrir(ios::in | ios::out);
        vector<char> zeros(br.tam_bitmap_setores * TAM_SETOR, 0);
        fs.seekp((long)br.inicio_bitmap * TAM_SETOR);
        fs.write(zeros.data(), zeros.size());

        for(uint32_t i=0; i < br.inicio_dados; i++) set_bitmap(fs, i, true);
        
        // Zera o diretório raiz
        vector<char> root_z(br.tam_root_setores * TAM_SETOR, 0);
        fs.seekp((long)br.inicio_root * TAM_SETOR);
        fs.write(root_z.data(), root_z.size());

        cout << "Disco formatado com sucesso (" << total_setores << " setores)." << endl;
    }

    // Comando: listar - exibe todos os arquivos válidos no diretório raiz
    void listar() {
        carregar_metadados();
        fstream f = abrir(ios::in);
        f.seekg((long)br.inicio_root * TAM_SETOR);
        
        EntradaDir e;
        cout << left << setw(20) << "NOME" << setw(10) << "EXT" << setw(10) << "TAMANHO" << "LBA INICIAL" << endl;
        cout << string(60, '-') << endl;
        
        for(int i=0; i < br.max_entradas_root; i++) {
            f.read((char*)&e, sizeof(e));
            if (e.atributo == ATTR_LIVRE) break; // Otimização: para ao encontrar entrada livre
            if (e.atributo == ATTR_VALIDO || e.atributo == ATTR_MODIFICADO) {
                cout << left << setw(20) << limpar_string(string(e.nome, 16))
                     << setw(10) << limpar_string(string(e.ext, 3))
                     << setw(10) << e.tamanho_bytes 
                     << e.primeiro_setor << endl;
            }
        }
    }

    // Comando: importar - copia um arquivo do PC para o sistema CBFS
    void importar(string pc_origem) {
        carregar_metadados();
        ifstream arq_in(pc_origem, ios::binary | ios::ate);
        if (!arq_in) throw runtime_error("Arquivo de origem nao encontrado no PC.");
        uint32_t tam = arq_in.tellg();                    // Tamanho em bytes
        arq_in.seekg(0);

        // Calcula quantos setores são necessários (arredondando para cima)
        uint32_t setores_nec = (tam + TAM_SETOR - 1) / TAM_SETOR;
        if (setores_nec == 0) setores_nec = 1;

        fstream f = abrir(ios::in | ios::out);

        // Extrai nome e extensão do arquivo de origem
        string base = pc_origem.substr(pc_origem.find_last_of("/\\") + 1);
        size_t ponto = base.find_last_of('.');
        string n_novo = (ponto == string::npos) ? base : base.substr(0, ponto);
        string ex_novo = (ponto == string::npos) ? "" : base.substr(ponto + 1);
        
        // Converte para maiúsculas (conforme especificação)
        transform(n_novo.begin(), n_novo.end(), n_novo.begin(), ::toupper);
        transform(ex_novo.begin(), ex_novo.end(), ex_novo.begin(), ::toupper);
        string nome_completo_novo = n_novo + (ex_novo.empty() ? "" : "." + ex_novo);

        // Procura por arquivo duplicado e localiza primeira entrada livre
        f.seekg((long)br.inicio_root * TAM_SETOR);
        EntradaDir e_temp;
        int idx_vaga = -1;
        
        for(int i=0; i < br.max_entradas_root; i++) {
            f.read((char*)&e_temp, sizeof(e_temp));
            
            // Verifica se já existe arquivo com mesmo nome (válido ou modificado)
            if (e_temp.atributo == ATTR_VALIDO || e_temp.atributo == ATTR_MODIFICADO) {
                string n_existente = limpar_string(string(e_temp.nome, 16));
                string ex_existente = limpar_string(string(e_temp.ext, 3));
                string existente = n_existente + (ex_existente.empty() ? "" : "." + ex_existente);
                
                if (existente == nome_completo_novo) {
                    throw runtime_error("Erro: Ja existe um arquivo com este nome ('" + nome_completo_novo + "').");
                }
            }

            // Guarda a primeira entrada livre ou excluída encontrada
            if (idx_vaga == -1 && (e_temp.atributo == ATTR_LIVRE || e_temp.atributo == ATTR_EXCLUIDO)) {
                idx_vaga = i;
            }
        }
        if (idx_vaga == -1) throw runtime_error("Diretorio Raiz cheio.");

        // Busca espaço contíguo na região de dados
        uint32_t setor_inicial = buscar_espaco_livre(f, setores_nec);

        // Copia os dados do arquivo para os setores alocados
        vector<char> buffer(TAM_SETOR);
        for(uint32_t i=0; i < setores_nec; i++) {
            memset(buffer.data(), 0, TAM_SETOR);          // Preenche com zeros
            if (arq_in.tellg() < tam) arq_in.read(buffer.data(), TAM_SETOR);
            
            f.seekp((long)(setor_inicial + i) * TAM_SETOR);
            f.write(buffer.data(), TAM_SETOR);
            set_bitmap(f, setor_inicial + i, true);       // Marca setor como ocupado
        }

        // Cria e escreve a entrada no diretório raiz
        EntradaDir e;
        memset(&e, 0, sizeof(e));
        memset(e.nome, ' ', 16); memcpy(e.nome, n_novo.c_str(), min((size_t)16, n_novo.size()));
        memset(e.ext, ' ', 3);   memcpy(e.ext, ex_novo.c_str(), min((size_t)3, ex_novo.size()));
        
        e.atributo = ATTR_VALIDO;
        e.tamanho_bytes = tam;
        e.primeiro_setor = setor_inicial;
        e.num_setores = setores_nec;

        f.seekp((long)br.inicio_root * TAM_SETOR + (idx_vaga * sizeof(EntradaDir)));
        f.write((char*)&e, sizeof(e));

        cout << "Arquivo importado com sucesso (Setor " << setor_inicial << ")." << endl;
    }

    // Comando: exportar - copia um arquivo do CBFS para o PC
    void exportar(string nome_cbfs) {
        carregar_metadados();
        string busca = nome_cbfs;
        transform(busca.begin(), busca.end(), busca.begin(), ::toupper);

        fstream f = abrir(ios::in);
        f.seekg((long)br.inicio_root * TAM_SETOR);
        EntradaDir e;
        
        // Procura o arquivo no diretório raiz
        for(int i=0; i < br.max_entradas_root; i++) {
            f.read((char*)&e, sizeof(e));
            if (e.atributo != ATTR_VALIDO && e.atributo != ATTR_MODIFICADO) continue;

            string n = limpar_string(string(e.nome, 16));
            string ex = limpar_string(string(e.ext, 3));
            string completo = n + (ex.empty() ? "" : "." + ex);

            if (completo == busca) {
                // Arquivo encontrado: copia seus dados setor por setor
                ofstream arq_out(nome_cbfs, ios::binary);
                f.seekg((long)e.primeiro_setor * TAM_SETOR);
                
                vector<char> buf(TAM_SETOR);
                uint32_t resta = e.tamanho_bytes;
                for(uint32_t s=0; s < e.num_setores; s++) {
                    f.read(buf.data(), TAM_SETOR);
                    uint32_t write_sz = (resta > TAM_SETOR) ? TAM_SETOR : resta;
                    arq_out.write(buf.data(), write_sz);
                    resta -= write_sz;
                }
                cout << "Arquivo exportado com sucesso!" << endl;
                return;
            }
        }
        throw runtime_error("Arquivo nao encontrado no CBFS.");
    }

    // Comando: remover - exclui logicamente um arquivo (marca como excluído e libera bitmap)
    void remover(string nome) {
        carregar_metadados();
        string busca = nome;
        transform(busca.begin(), busca.end(), busca.begin(), ::toupper);

        fstream f = abrir(ios::in | ios::out);
        f.seekg((long)br.inicio_root * TAM_SETOR);
        EntradaDir e;
        long pos_leitura;

        for(int i=0; i < br.max_entradas_root; i++) {
            pos_leitura = f.tellg();
            f.read((char*)&e, sizeof(e));
            if (e.atributo != ATTR_VALIDO && e.atributo != ATTR_MODIFICADO) continue;

            string n = limpar_string(string(e.nome, 16));
            string ex = limpar_string(string(e.ext, 3));
            string completo = n + (ex.empty() ? "" : "." + ex);

            if (completo == busca) {
                // Marca o atributo como excluído (0x20)
                f.seekp(pos_leitura + 19); 
                f.write((char*)&ATTR_EXCLUIDO, 1);
                
                // Libera os setores no bitmap
                for(uint32_t s=0; s < e.num_setores; s++) {
                    set_bitmap(f, e.primeiro_setor + s, false);
                }
                cout << "Arquivo removido com sucesso." << endl;
                return;
            }
        }
        throw runtime_error("Arquivo nao encontrado para remocao.");
    }

    // Comando: status - exibe mapa visual da ocupação do disco e estatísticas
    void status() {
        carregar_metadados();
        fstream f = abrir(ios::in);
        
        // Lê o bitmap inteiro
        vector<uint8_t> bmp(br.tam_bitmap_setores * TAM_SETOR);
        f.seekg((long)br.inicio_bitmap * TAM_SETOR);
        f.read((char*)bmp.data(), bmp.size());

        uint32_t ocupados = 0;
        uint32_t livres = 0;
        
        cout << endl << "--- MAPA DO DISCO (Visualizacao Simplificada) ---" << endl;
        cout << "[.] Livre  [#] Ocupado (Sistema/Arquivos)" << endl;
        cout << "------------------------------------------------" << endl;

        // Escala para não imprimir um caractere por setor se o disco for grande
        int escala = max(1, (int)br.total_setores / 64); 
        
        for (uint32_t i = 0; i < br.total_setores; i++) {
            bool is_ocupado = (bmp[i/8] >> (i%8)) & 1;
            
            if (is_ocupado) ocupados++;
            else livres++;

            // Imprime caractere representativo a cada 'escala' setores
            if (i % escala == 0) {
                cout << (is_ocupado ? "#" : ".");
            }
        }
        cout << endl << string(48, '-') << endl;
        cout << "Setores Totais: " << br.total_setores << endl;
        cout << "Setores Usados: " << ocupados << " (" << (ocupados*100/br.total_setores) << "%)" << endl;
        cout << "Setores Livres: " << livres << endl;
        cout << "Capacidade:     " << (br.total_setores * TAM_SETOR) / 1024 << " KB" << endl;
        cout << endl;
    }
};

// Função principal: interpreta os argumentos da linha de comando
int main(int argc, char* argv[]) {
    if (argc < 3) {
        cout << "Sistema de Arquivos CBFS - Manual de Uso:" << endl;
        cout << "./cbfs formatar <disco> <setores>" << endl;
        cout << "./cbfs listar   <disco>" << endl;
        cout << "./cbfs importar <disco> <arquivo_pc>" << endl;
        cout << "./cbfs exportar <disco> <nome_do_aqruivo_no_cbfs>" << endl;
        cout << "./cbfs remover  <disco> <nome_do_aqruivo_no_cbfs>" << endl;
        cout << "./cbfs status   <disco>" << endl; 
        return 1;
    }

    try {
        CBFS fs(argv[2]);                     // Inicializa o objeto CBFS com o arquivo de disco
        string cmd = argv[1];

        // Executa o comando correspondente
        if (cmd == "formatar") {
            if (argc < 4) throw runtime_error("O comando 'formatar' requer o tamanho do disco.");
            fs.formatar(stoi(argv[3]));
        }
        else if (cmd == "listar") fs.listar();
        else if (cmd == "importar") fs.importar(argv[3]);
        else if (cmd == "exportar") fs.exportar(argv[3]);
        else if (cmd == "remover") fs.remover(argv[3]);
        else if (cmd == "status") fs.status();
        else cout << "Comando invalido!" << endl;
        
    } catch(exception &ex) {
        cerr << "Erro: " << ex.what() << endl;
    }
    return 0;
}