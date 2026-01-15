#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <algorithm>

using namespace std;

// --- Configurações do Sistema ---
const int TAM_SETOR = 512;
const uint16_t ASSINATURA = 0xAA55;
const int TAM_ENTRADA = 32;
const uint32_t FIM_CADEIA = 0xFFFFFFFF;

// Estrutura do Setor de Boot (Boot Record)
#pragma pack(push, 1)
struct BootRecord {
    uint8_t jump[3];
    char oem_name[8];
    uint16_t bytes_por_setor;    // Offset 0x0B
    uint8_t setores_por_cluster; // Offset 0x0D
    uint16_t setores_reservados; // Offset 0x0E
    uint8_t num_bitmaps;
    uint16_t entradas_raiz;
    uint16_t setores_totais;
    uint16_t tam_bitmap;         // Tamanho do Bitmap em setores
    uint16_t tam_indice;         // Tamanho da Região de Índices em setores
    uint16_t tam_raiz;           // Tamanho do Diretório Raiz em setores
    uint8_t padding[460];        // Preenchimento para alinhar a assinatura
    uint16_t assinatura;         // Offset 0x1FE (510)
};

// Estrutura de uma entrada no Diretório Raiz
struct EntradaDir {
    char nome[8];
    char ext[3];
    uint8_t atributo;
    uint8_t reservado[12];
    uint32_t ptr_indice;         // Ponteiro para o primeiro Bloco de Índice
    uint32_t tamanho;            // Tamanho do arquivo em bytes
};
#pragma pack(pop)

class CBFS {
    string dispositivo;
    BootRecord br;
    long off_bitmap, off_indice, off_raiz, off_dados;
    int tam_cluster;

    // Função simples para abrir o arquivo do disco
    fstream abrir(ios::openmode modo) {
        fstream f(dispositivo, modo | ios::binary);
        if (!f.is_open()) throw runtime_error("Erro ao abrir o dispositivo: " + dispositivo);
        return f;
    }

    // Lê o setor de boot e calcula onde começa cada região do disco
    void carregar_metadados() {
        fstream f = abrir(ios::in);
        f.read((char*)&br, sizeof(br));
        if (br.assinatura != ASSINATURA) throw runtime_error("Disco nao formatado como CBFS.");
        
        // Cálculo dos offsets (posições) de cada área
        off_bitmap = (long)br.setores_reservados * TAM_SETOR;
        off_indice = off_bitmap + ((long)br.tam_bitmap * TAM_SETOR);
        off_raiz   = off_indice + ((long)br.tam_indice * TAM_SETOR);
        off_dados  = off_raiz   + ((long)br.tam_raiz * TAM_SETOR);
        tam_cluster = br.bytes_por_setor * br.setores_por_cluster;
    }

    // Procura um bloco de índice que esteja todo zerado (vazio)
    uint32_t buscar_bloco_indice_livre(fstream &f) {
        vector<char> buf(tam_cluster);
        int total_blocos = (br.tam_indice * TAM_SETOR) / tam_cluster;
        for (int i = 0; i < total_blocos; i++) {
            f.seekg(off_indice + (long)i * tam_cluster);
            f.read(buf.data(), tam_cluster);
            bool vazio = true;
            for (char c : buf) if (c != 0) { vazio = false; break; }
            if (vazio) return i;
        }
        throw runtime_error("Nao ha mais espaco na Regiao de Indices.");
    }

    // Procura um bit 0 no Bitmap para alocar um cluster de dados
    uint32_t alocar_cluster_dados(fstream &f) {
        f.seekg(off_bitmap);
        vector<uint8_t> bmp(br.tam_bitmap * TAM_SETOR);
        f.read((char*)bmp.data(), bmp.size());
        for (int i = 0; i < bmp.size() * 8; i++) {
            if (!(bmp[i/8] & (1 << (i%8)))) {
                bmp[i/8] |= (1 << (i%8)); // Marca como ocupado (1)
                f.seekp(off_bitmap);
                f.write((char*)bmp.data(), bmp.size());
                return i + 2; // Clusters de dados começam em 2
            }
        }
        throw runtime_error("Disco cheio (Regiao de Dados).");
    }

    // Marca um bit como 0 no Bitmap para liberar o cluster
    void liberar_cluster_dados(fstream &f, uint32_t cl) {
        if (cl < 2) return;
        f.seekg(off_bitmap);
        vector<uint8_t> bmp(br.tam_bitmap * TAM_SETOR);
        f.read((char*)bmp.data(), bmp.size());
        bmp[(cl-2)/8] &= ~(1 << ((cl-2)%8)); // Zera o bit
        f.seekp(off_bitmap);
        f.write((char*)bmp.data(), bmp.size());
    }

    // Função auxiliar para limpar espaços em branco de strings
    string limpar_string(string s) {
        s.erase(s.find_last_not_of(' ') + 1);
        return s;
    }

public:
    CBFS(string d) : dispositivo(d) {}

    // Formata o disco criando as estruturas iniciais
    void formatar(int total_setores) {
        memset(&br, 0, sizeof(br));
        br.jump[0] = 0xEB; br.jump[1] = 0x3C; br.jump[2] = 0x90;
        memcpy(br.oem_name, "CBFS_PTV ", 8);
        br.bytes_por_setor = TAM_SETOR;
        br.setores_por_cluster = 1;
        br.setores_reservados = 1;
        br.entradas_raiz = 128;
        br.setores_totais = total_setores;
        br.tam_raiz = 8; 
        br.tam_indice = ceil(total_setores * 0.05); // 5% do disco para índices
        br.tam_bitmap = ceil(total_setores / (512.0 * 8.0));
        br.assinatura = ASSINATURA;

        ofstream f(dispositivo, ios::binary);
        f.write((char*)&br, sizeof(br));
        
        // Zera as regiões de metadados
        vector<char> zeros(TAM_SETOR, 0);
        int meta_setores = br.tam_bitmap + br.tam_indice + br.tam_raiz;
        for(int i=0; i < meta_setores; i++) f.write(zeros.data(), TAM_SETOR);
        
        // Garante o tamanho final do arquivo
        f.seekp((long)total_setores * TAM_SETOR - 1);
        char z = 0; f.write(&z, 1);
        f.close();
        cout << "Disco formatado com sucesso (" << total_setores << " setores)." << endl;
    }

    // Lista os arquivos presentes no diretório raiz
    void listar() {
        carregar_metadados();
        fstream f = abrir(ios::in);
        f.seekg(off_raiz);
        EntradaDir e;
        cout << left << setw(15) << "NOME" << setw(12) << "TAMANHO" << "ID_INDICE" << endl;
        cout << string(40, '-') << endl;
        for(int i=0; i<128; i++) {
            f.read((char*)&e, sizeof(e));
            if (e.nome[0] != 0 && (uint8_t)e.nome[0] != 0xE5) {
                string n(e.nome, 8); n = limpar_string(n);
                string ex(e.ext, 3); ex = limpar_string(ex);
                string nome_completo = n + (ex.empty() ? "" : "." + ex);
                cout << left << setw(15) << nome_completo << setw(12) << e.tamanho << e.ptr_indice << endl;
            }
        }
    }

    // Copia um arquivo do PC para o CBFS
    void importar(string pc_origem) {
        carregar_metadados();
        ifstream arq_in(pc_origem, ios::binary | ios::ate);
        if (!arq_in) throw runtime_error("Arquivo de origem nao encontrado no PC.");
        uint32_t tam = arq_in.tellg();
        arq_in.seekg(0);

        fstream f = abrir(ios::in | ios::out);
        
        // 1. Achar vaga no diretório
        f.seekg(off_raiz);
        EntradaDir e;
        int idx_vaga = -1;
        for(int i=0; i<128; i++) {
            f.read((char*)&e, sizeof(e));
            if (e.nome[0] == 0 || (uint8_t)e.nome[0] == 0xE5) { idx_vaga = i; break; }
        }
        if (idx_vaga == -1) throw runtime_error("Diretorio Raiz cheio.");

        // 2. Alocar blocos e copiar dados
        uint32_t bloco_idx = buscar_bloco_indice_livre(f);
        uint32_t primeiro_bloco = bloco_idx;
        uint32_t escritos = 0;
        int slot = 0, max_slots = (tam_cluster/4)-1;
        vector<char> buffer(tam_cluster);

        while(escritos < tam) {
            uint32_t cl_dado = alocar_cluster_dados(f);
            int ler = min((uint32_t)tam_cluster, tam - escritos);
            arq_in.read(buffer.data(), ler);
            
            // Escreve o dado
            f.seekp(off_dados + (long)(cl_dado - 2) * tam_cluster);
            f.write(buffer.data(), tam_cluster);

            // Registra no índice
            f.seekp(off_indice + (long)bloco_idx * tam_cluster + (slot * 4));
            f.write((char*)&cl_dado, 4);
            
            escritos += ler; slot++;
            
            // Se o bloco de índice encher, aloca outro
            if (slot == max_slots && escritos < tam) {
                uint32_t novo = buscar_bloco_indice_livre(f);
                f.seekp(off_indice + (long)bloco_idx * tam_cluster + (max_slots * 4));
                f.write((char*)&novo, 4);
                bloco_idx = novo; slot = 0;
            }
        }
        // Marca o fim da cadeia de índices
        uint32_t fim = FIM_CADEIA;
        f.seekp(off_indice + (long)bloco_idx * tam_cluster + (slot * 4)); f.write((char*)&fim, 4);

        // 3. Gravar entrada no diretório
        memset(&e, 0, sizeof(e));
        memset(e.nome, ' ', 8); memset(e.ext, ' ', 3);
        
        // Extrai apenas o nome do arquivo (sem o caminho)
        string base = pc_origem.substr(pc_origem.find_last_of("/\\") + 1);
        size_t ponto = base.find_last_of('.');
        string n = (ponto == string::npos) ? base : base.substr(0, ponto);
        string ex = (ponto == string::npos) ? "" : base.substr(ponto + 1);
        
        memcpy(e.nome, n.c_str(), min((int)n.size(), 8));
        memcpy(e.ext, ex.c_str(), min((int)ex.size(), 3));
        e.ptr_indice = primeiro_bloco;
        e.tamanho = tam;
        
        f.seekp(off_raiz + idx_vaga * TAM_ENTRADA);
        f.write((char*)&e, sizeof(e));
        cout << "Arquivo '" << base << "' importado com sucesso." << endl;
    }

    // Copia um arquivo do CBFS para o PC
    void exportar(string nome_cbfs, string pc_destino) {
        carregar_metadados();
        
        // Converte busca para maiúsculo para facilitar
        string busca = nome_cbfs;
        transform(busca.begin(), busca.end(), busca.begin(), ::toupper);

        fstream f = abrir(ios::in);
        f.seekg(off_raiz);
        EntradaDir e;
        bool achou = false;
        for(int i=0; i<128; i++) {
            f.read((char*)&e, sizeof(e));
            if (e.nome[0] != 0 && (uint8_t)e.nome[0] != 0xE5) {
                string n(e.nome, 8); n = limpar_string(n);
                string ex(e.ext, 3); ex = limpar_string(ex);
                string completo = n + (ex.empty() ? "" : "." + ex);
                
                string n_upper = completo;
                transform(n_upper.begin(), n_upper.end(), n_upper.begin(), ::toupper);
                
                if (n_upper == busca) { achou = true; break; }
            }
        }
        if (!achou) throw runtime_error("Arquivo nao encontrado no CBFS.");

        ofstream arq_out(pc_destino, ios::binary);
        uint32_t bloco_idx = e.ptr_indice;
        uint32_t restante = e.tamanho;
        vector<char> buffer(tam_cluster);

        while(bloco_idx != FIM_CADEIA && restante > 0) {
            for(int s=0; s<(tam_cluster/4)-1 && restante > 0; s++) {
                uint32_t cl_dado;
                f.seekg(off_indice + (long)bloco_idx * tam_cluster + (s*4));
                f.read((char*)&cl_dado, 4);
                
                f.seekg(off_dados + (long)(cl_dado - 2) * tam_cluster);
                int a_ler = min((uint32_t)tam_cluster, restante);
                f.read(buffer.data(), a_ler);
                arq_out.write(buffer.data(), a_ler);
                restante -= a_ler;
            }
            // Pula para o próximo bloco de índice (último slot)
            f.seekg(off_indice + (long)bloco_idx * tam_cluster + (tam_cluster - 4));
            f.read((char*)&bloco_idx, 4);
        }
        cout << "Arquivo exportado com sucesso para: " << pc_destino << endl;
    }

    // Remove um arquivo do sistema
    void remover(string nome) {
        carregar_metadados();
        
        string busca = nome;
        transform(busca.begin(), busca.end(), busca.begin(), ::toupper);

        fstream f = abrir(ios::in | ios::out);
        f.seekg(off_raiz);
        EntradaDir e;
        long pos_entrada;
        for(int i=0; i<128; i++) {
            pos_entrada = f.tellg();
            f.read((char*)&e, sizeof(e));
            if (e.nome[0] == 0 || (uint8_t)e.nome[0] == 0xE5) continue;

            string n(e.nome, 8); n = limpar_string(n);
            string ex(e.ext, 3); ex = limpar_string(ex);
            string completo = n + (ex.empty() ? "" : "." + ex);

            string n_upper = completo;
            transform(n_upper.begin(), n_upper.end(), n_upper.begin(), ::toupper);

            if (n_upper == busca) {
                uint32_t b_idx = e.ptr_indice;
                while(b_idx != FIM_CADEIA && b_idx != 0) {
                    // 1. Lê o próximo índice ANTES de zerar o atual
                    uint32_t prox;
                    f.seekg(off_indice + (long)b_idx * tam_cluster + (tam_cluster - 4));
                    f.read((char*)&prox, 4);

                    // 2. Libera os clusters de dados apontados por este bloco
                    for(int s=0; s<(tam_cluster/4)-1; s++) {
                        uint32_t cl;
                        f.seekg(off_indice + (long)b_idx * tam_cluster + (s*4));
                        f.read((char*)&cl, 4);
                        if (cl >= 2 && cl != FIM_CADEIA) liberar_cluster_dados(f, cl);
                    }
                    
                    // 3. Zera o bloco de índice atual no disco
                    vector<char> zeros(tam_cluster, 0);
                    f.seekp(off_indice + (long)b_idx * tam_cluster);
                    f.write(zeros.data(), tam_cluster);
                    
                    // 4. Avança para o próximo bloco da cadeia
                    b_idx = prox;
                }
                // Marca a entrada como excluída no diretório
                f.seekp(pos_entrada); char mark = 0xE5; f.write(&mark, 1);
                cout << "Arquivo '" << nome << "' removido com sucesso." << endl;
                return;
            }
        }
        throw runtime_error("Arquivo nao encontrado para remocao.");
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cout << "Sistema de Arquivos CBFS - Manual de Uso:" << endl;
        cout << "./cbfs formatar <disco> <setores>" << endl;
        cout << "./cbfs listar   <disco>" << endl;
        cout << "./cbfs importar <disco> <arquivo_pc>" << endl;
        cout << "./cbfs exportar <disco> <nome_cbfs> <destino_pc>" << endl;
        cout << "./cbfs remover  <disco> <nome_cbfs>" << endl;
        return 1;
    }
    try {
        CBFS fs(argv[2]);
        string cmd = argv[1];
        if (cmd == "formatar") fs.formatar(stoi(argv[3]));
        else if (cmd == "listar") fs.listar();
        else if (cmd == "importar") fs.importar(argv[3]);
        else if (cmd == "exportar") fs.exportar(argv[3], argv[4]);
        else if (cmd == "remover") fs.remover(argv[3]);
        else cout << "Comando invalido!" << endl;
    } catch(exception &ex) {
        cerr << "Erro: " << ex.what() << endl;
    }
    return 0;
}
