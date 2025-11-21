#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace std;

/*********************************** Constantes ***********************************/
#define BYTES_PER_SECTOR 512
#define RESERVED_SECTORS 1
#define ROOT_DIR_ENTRIES 128
#define ROOT_DIR_SIZE 8 // Tamanho em Setores
#define BITMAP_START 1  // Em Setor
#define N 30000         // Total de setores do disco

/*********************************** Estruturas do Sistema de Arquivos ***********************************/
// Setor de Boot (Boot Record)
typedef struct {
    uint16_t bytes_per_sector;
    uint16_t reserved_sectors;
    uint16_t root_dir_entries;
    uint16_t root_dir_size;
    uint32_t total_sectors;
    uint32_t bitmap_start;
    uint32_t bitmap_size;
    uint32_t root_dir_start;
    uint32_t data_region_start;
    char file_system_name[8];
    uint16_t boot_signature;
} Boot_Record;

// Bitmap de Espaço Livre
typedef struct {
    vector<uint8_t> bits;
} Bitmap;

// Diretório Raiz (Root Directory)
typedef struct {
    char file_name[16];
    char extension[3];
    uint8_t atributes;
    uint32_t file_size;
    uint32_t first_sector;
    uint32_t number_of_sectors;
} Root_Directory;

// Área de Dados (Data Region)
typedef struct {
    vector<uint8_t> file_content;
} Data_Region;

/*********************************** Funções ***********************************/
// Função para preecher as informações do Boot Record
Boot_Record fill_BT() {
    Boot_Record bt;
    int n = N;

    bt.bytes_per_sector = BYTES_PER_SECTOR;
    bt.reserved_sectors = RESERVED_SECTORS;
    bt.root_dir_entries = ROOT_DIR_ENTRIES;
    bt.root_dir_size = ROOT_DIR_SIZE;
    bt.total_sectors = N;
    bt.bitmap_start = BITMAP_START;
    bt.bitmap_size = (n + 4096 - 1) / 4096;
    bt.root_dir_start = 1 + bt.bitmap_size;
    bt.data_region_start = bt.root_dir_start + bt.root_dir_size;
    memcpy(bt.file_system_name, "CBFS    ", 8);
    bt.boot_signature = 0x7777;

    return bt;
}

// Função para preencher com zeros o restante do Boot Record
void write_boot_record(FILE *img, Boot_Record bt) {
    uint8_t buffer[512];

    // Preenche todo o setor com zeros
    memset(buffer, 0, 512);

    // Copia o struct para o início do setor
    memcpy(buffer, &bt, sizeof(Boot_Record));

    // Escreve o setor no arquivo
    fwrite(buffer, 1, 512, img);
}

/*********************************** Função Principal ***********************************/
int main(int argc, char *argv[]) {
    if (argc != 2) {
        cout << "Uso: ./Formatador <Nome da imagem (.img)>" << endl;
        exit(1);
    }

    FILE *Img = fopen(argv[1], "wb");
    if (!Img) {
        cout << "Erro: Arquivo " << argv[1] << " não pode ser aberto!" << endl;
        exit(1);
    }

    // Escreve o Boot Record no .Img
    Boot_Record boot_record = fill_BT();
    write_boot_record(Img, boot_record);

    cout << "Arquivo " << argv[1] << " formatado com sucesso!" << endl;

    return 0;
}
