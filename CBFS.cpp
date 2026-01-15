#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <iomanip>

using namespace std;

// --- Configurações ---
const int TAM_SETOR = 512;
const uint16_t ASSINATURA_BOOT = 0x7777; 
const string NOME_SISTEMA = "CBFS";      

// Estrutura do Setor de Boot 
#pragma pack(push, 1)
struct BootRecord {
    uint16_t bytes_por_setor;       // Offset 0
    uint16_t setores_reservados;    // Offset 2
    uint16_t max_entradas_root;     // Offset 4
    uint16_t tam_root_setores;      // Offset 6
    uint32_t total_setores;         // Offset 8
    uint32_t inicio_bitmap;         // Offset 12
    uint32_t tam_bitmap_setores;    // Offset 16
    uint32_t inicio_root;           // Offset 20
    uint32_t inicio_dados;          // Offset 24
    uint8_t  padding_gap[4];        // Offset 28-32
    char     nome_sistema[4];       // Offset 32
    uint16_t assinatura;            // Offset 36
    uint8_t  padding_final[474];    // Preencher até 512 bytes
};

// Estrutura da Entrada de Diretório
struct EntradaDir {
    char     nome[16];          // Offset 0
    char     ext[3];            // Offset 16
    uint8_t  atributo;          // Offset 19
    uint32_t tamanho_bytes;     // Offset 20
    uint32_t primeiro_setor;    // Offset 24
    uint32_t num_setores;       // Offset 28
};
#pragma pack(pop)

// Atributos
const uint8_t ATTR_LIVRE = 0x00;
const uint8_t ATTR_VALIDO = 0x10;
const uint8_t ATTR_EXCLUIDO = 0x20;
const uint8_t ATTR_MODIFICADO = 0x30;

class CBFS {
    string dispositivo;
    BootRecord br;

    // Abre o dispositivo
    fstream abrir(ios::openmode modo) {
        fstream f(dispositivo, modo | ios::binary);
        if (!f.is_open()) throw runtime_error("Erro ao abrir o dispositivo: " + dispositivo);
        return f;
    }

    // Lê metadados
    void carregar_metadados() {
        fstream f = abrir(ios::in);
        f.read((char*)&br, sizeof(BootRecord));
        if (br.assinatura != ASSINATURA_BOOT) 
            throw runtime_error("Disco nao formatado como CBFS (Assinatura invalida).");
    }

    // Auxiliar: Limpa espaços de strings
    string limpar_string(string s) {
        size_t last = s.find_last_not_of(' ');
        if (last == string::npos) return "";
        return s.substr(0, last + 1);
    }

    // Define bit no Bitmap
    void set_bitmap(fstream &f, uint32_t setor, bool ocupado) {
        long offset = (long)br.inicio_bitmap * TAM_SETOR + (setor / 8);
        int bit = setor % 8;
        
        char byte;
        f.seekg(offset);
        f.read(&byte, 1);
        
        if (ocupado) byte |= (1 << bit);
        else byte &= ~(1 << bit);
        
        f.seekp(offset);
        f.write(&byte, 1);
    }

    // Busca espaço CONTÍGUO 
    uint32_t buscar_espaco_livre(fstream &f, uint32_t qtd_setores) {
        vector<uint8_t> bmp(br.tam_bitmap_setores * TAM_SETOR);
        f.seekg((long)br.inicio_bitmap * TAM_SETOR);
        f.read((char*)bmp.data(), bmp.size());

        uint32_t contador = 0;
        uint32_t inicio_candidato = 0;

        // Tenta achar buraco contíguo
        for(uint32_t i = br.inicio_dados; i < br.total_setores; i++) {
            bool ocupado = (bmp[i/8] >> (i%8)) & 1;
            
            if (!ocupado) {
                if (contador == 0) inicio_candidato = i;
                contador++;
                if (contador == qtd_setores) return inicio_candidato;
            } else {
                contador = 0;
            }
        }

        // --- Diagnóstico de Erro ---
        uint32_t total_livres = 0;
        for(uint32_t i = br.inicio_dados; i < br.total_setores; i++) {
            if (!((bmp[i/8] >> (i%8)) & 1)) total_livres++;
        }

        if (total_livres < qtd_setores) {
            throw runtime_error("Erro: Disco cheio! Espaco livre insuficiente.");
        } else {
            throw runtime_error("Erro: Fragmentacao! Ha espaco livre, mas nao contiguo.");
        }
    }

public:
    CBFS(string d) : dispositivo(d) {}

    // Formatar
    void formatar(int total_setores) {
        memset(&br, 0, sizeof(br));
        br.bytes_por_setor = TAM_SETOR;
        br.setores_reservados = 1; 
        br.max_entradas_root = 128;
        br.tam_root_setores = 8;
        br.total_setores = total_setores;
        
        br.inicio_bitmap = 1;
        br.tam_bitmap_setores = (total_setores + 4095) / 4096;
        if (br.tam_bitmap_setores == 0) br.tam_bitmap_setores = 1;
        
        br.inicio_root = br.inicio_bitmap + br.tam_bitmap_setores;
        br.inicio_dados = br.inicio_root + br.tam_root_setores;
        
        strncpy(br.nome_sistema, NOME_SISTEMA.c_str(), 4);
        br.assinatura = ASSINATURA_BOOT;

        ofstream f(dispositivo, ios::binary);
        if (!f) throw runtime_error("Falha ao criar arquivo de disco.");
        
        f.write((char*)&br, sizeof(br));
        f.seekp((long)total_setores * TAM_SETOR - 1);
        char z = 0; f.write(&z, 1);
        f.close();

        fstream fs = abrir(ios::in | ios::out);
        vector<char> zeros(br.tam_bitmap_setores * TAM_SETOR, 0);
        fs.seekp((long)br.inicio_bitmap * TAM_SETOR);
        fs.write(zeros.data(), zeros.size());

        for(uint32_t i=0; i < br.inicio_dados; i++) set_bitmap(fs, i, true);
        
        vector<char> root_z(br.tam_root_setores * TAM_SETOR, 0);
        fs.seekp((long)br.inicio_root * TAM_SETOR);
        fs.write(root_z.data(), root_z.size());

        cout << "Disco formatado com sucesso (" << total_setores << " setores)." << endl;
    }

    // Listar
    void listar() {
        carregar_metadados();
        fstream f = abrir(ios::in);
        f.seekg((long)br.inicio_root * TAM_SETOR);
        
        EntradaDir e;
        cout << left << setw(20) << "NOME" << setw(10) << "EXT" << setw(10) << "TAMANHO" << "LBA INICIAL" << endl;
        cout << string(60, '-') << endl;
        
        for(int i=0; i < br.max_entradas_root; i++) {
            f.read((char*)&e, sizeof(e));
            if (e.atributo == ATTR_LIVRE) break; 
            if (e.atributo == ATTR_VALIDO || e.atributo == ATTR_MODIFICADO) {
                cout << left << setw(20) << limpar_string(string(e.nome, 16))
                     << setw(10) << limpar_string(string(e.ext, 3))
                     << setw(10) << e.tamanho_bytes 
                     << e.primeiro_setor << endl;
            }
        }
    }

    // Importar
    void importar(string pc_origem) {
        carregar_metadados();
        ifstream arq_in(pc_origem, ios::binary | ios::ate);
        if (!arq_in) throw runtime_error("Arquivo de origem nao encontrado no PC.");
        uint32_t tam = arq_in.tellg();
        arq_in.seekg(0);

        uint32_t setores_nec = (tam + TAM_SETOR - 1) / TAM_SETOR;
        if (setores_nec == 0) setores_nec = 1;

        fstream f = abrir(ios::in | ios::out);

        // Preparar nome do arquivo novo
        string base = pc_origem.substr(pc_origem.find_last_of("/\\") + 1);
        size_t ponto = base.find_last_of('.');
        string n_novo = (ponto == string::npos) ? base : base.substr(0, ponto);
        string ex_novo = (ponto == string::npos) ? "" : base.substr(ponto + 1);
        
        transform(n_novo.begin(), n_novo.end(), n_novo.begin(), ::toupper);
        transform(ex_novo.begin(), ex_novo.end(), ex_novo.begin(), ::toupper);
        string nome_completo_novo = n_novo + (ex_novo.empty() ? "" : "." + ex_novo);

        // --- Proteção contra arquivos duplicados ---
        f.seekg((long)br.inicio_root * TAM_SETOR);
        EntradaDir e_temp;
        int idx_vaga = -1;
        
        for(int i=0; i < br.max_entradas_root; i++) {
            f.read((char*)&e_temp, sizeof(e_temp));
            
            // Verifica há arquivos duplicados se o arquivo for válido
            if (e_temp.atributo == ATTR_VALIDO || e_temp.atributo == ATTR_MODIFICADO) {
                string n_existente = limpar_string(string(e_temp.nome, 16));
                string ex_existente = limpar_string(string(e_temp.ext, 3));
                string existente = n_existente + (ex_existente.empty() ? "" : "." + ex_existente);
                
                if (existente == nome_completo_novo) {
                    throw runtime_error("Erro: Ja existe um arquivo com este nome ('" + nome_completo_novo + "').");
                }
            }

            // Guarda a primeira vaga livre encontrada
            if (idx_vaga == -1 && (e_temp.atributo == ATTR_LIVRE || e_temp.atributo == ATTR_EXCLUIDO)) {
                idx_vaga = i;
            }
        }
        if (idx_vaga == -1) throw runtime_error("Diretorio Raiz cheio.");

        // Busca Espaço
        uint32_t setor_inicial = buscar_espaco_livre(f, setores_nec);

        // Copiar dados
        vector<char> buffer(TAM_SETOR);
        for(uint32_t i=0; i < setores_nec; i++) {
            memset(buffer.data(), 0, TAM_SETOR);
            if (arq_in.tellg() < tam) arq_in.read(buffer.data(), TAM_SETOR);
            
            f.seekp((long)(setor_inicial + i) * TAM_SETOR);
            f.write(buffer.data(), TAM_SETOR);
            set_bitmap(f, setor_inicial + i, true);
        }

        // Gravar no Diretório
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

    // Exportar
    void exportar(string nome_cbfs) {
        carregar_metadados();
        string busca = nome_cbfs;
        transform(busca.begin(), busca.end(), busca.begin(), ::toupper);

        fstream f = abrir(ios::in);
        f.seekg((long)br.inicio_root * TAM_SETOR);
        EntradaDir e;
        
        for(int i=0; i < br.max_entradas_root; i++) {
            f.read((char*)&e, sizeof(e));
            if (e.atributo != ATTR_VALIDO && e.atributo != ATTR_MODIFICADO) continue;

            string n = limpar_string(string(e.nome, 16));
            string ex = limpar_string(string(e.ext, 3));
            string completo = n + (ex.empty() ? "" : "." + ex);

            if (completo == busca) {
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

    // Remover
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
                // Marca excluído
                f.seekp(pos_leitura + 19); 
                f.write((char*)&ATTR_EXCLUIDO, 1);
                
                // Libera Bitmap
                for(uint32_t s=0; s < e.num_setores; s++) {
                    set_bitmap(f, e.primeiro_setor + s, false);
                }
                cout << "Arquivo removido com sucesso." << endl;
                return;
            }
        }
        throw runtime_error("Arquivo nao encontrado para remocao.");
    }

    // --- Status do Disco (Visualização) ---
    void status() {
        carregar_metadados();
        fstream f = abrir(ios::in);
        
        vector<uint8_t> bmp(br.tam_bitmap_setores * TAM_SETOR);
        f.seekg((long)br.inicio_bitmap * TAM_SETOR);
        f.read((char*)bmp.data(), bmp.size());

        uint32_t ocupados = 0;
        uint32_t livres = 0;
        
        cout << endl << "--- MAPA DO DISCO (Visualizacao Simplificada) ---" << endl;
        cout << "[.] Livre  [#] Ocupado (Sistema/Arquivos)" << endl;
        cout << "------------------------------------------------" << endl;

        // Escala para não encher a tela se o disco for muito grande
        int escala = max(1, (int)br.total_setores / 64); 
        
        for (uint32_t i = 0; i < br.total_setores; i++) {
            bool is_ocupado = (bmp[i/8] >> (i%8)) & 1;
            
            if (is_ocupado) ocupados++;
            else livres++;

            // Imprime 1 caractere a cada 'escala' setores
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

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cout << "Sistema de Arquivos CBFS - Manual de Uso:" << endl;
        cout << "./cbfs formatar <disco> <setores>" << endl;
        cout << "./cbfs listar   <disco>" << endl;
        cout << "./cbfs importar <disco> <arquivo_pc>" << endl;
        cout << "./cbfs exportar <disco> <nome_do_arquivo_no_cbfs>" << endl;
        cout << "./cbfs remover  <disco> <nome_do_arquivo_no_cbfs>" << endl;
        cout << "./cbfs status   <disco>" << endl; 
        return 1;
    }
    try {
        CBFS fs(argv[2]);
        string cmd = argv[1];
        if (cmd == "formatar") fs.formatar(stoi(argv[3]));
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
