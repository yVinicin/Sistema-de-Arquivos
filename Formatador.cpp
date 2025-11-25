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
/*********************************** Boot Record ***********************************/
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

// Função para escrever o Boot Record no .img
void write_boot_record(FILE *img, Boot_Record *bt) {
    fseek(img, 0, SEEK_SET);
    fwrite(bt, sizeof(Boot_Record), 1, img);
}

/*********************************** Bitmap ***********************************/
// Função para criar Bitmap com todos os setores livres
Bitmap Fill_Bitmap(uint32_t total_sectors) {
    Bitmap bm;

    // Número total de bytes necessários
    uint32_t total_bytes = (total_sectors + 7) / 8;

    // Cria todo o bitmap inicializado com 0 (tudo livre)
    bm.bits.assign(total_bytes, 0);

    return bm;
}

// Função para marcar o setor ocupado
void bitmap_mark(Bitmap &bm, uint32_t sector) {
    uint32_t byte_index = sector / 8;
    uint32_t bit_index  = sector % 8;

    bm.bits[byte_index] |= (1 << bit_index);
}

// Função para preecher os setores ocupados
Bitmap Fill_System_Bitmap(Boot_Record *bt) {
    Bitmap bm = Fill_Bitmap(bt->total_sectors);

    // Setor 0 = Boot Record
    bitmap_mark(bm, 0);

    // Setores do Bitmap
    for (uint32_t s = bt->bitmap_start; s < bt->bitmap_start + bt->bitmap_size; s++) {
        bitmap_mark(bm, s);
    }

    // Setores do Diretório Raiz
    for (uint32_t s = bt->root_dir_start; s < bt->root_dir_start + bt->root_dir_size; s++) {
        bitmap_mark(bm, s);
    }

    return bm;
}

// Função para escrever o Bitmap no .img
void write_bitmap(FILE *img, Boot_Record *bt, Bitmap *bm) {
    long offset = bt->bitmap_start * BYTES_PER_SECTOR;
    fseek(img, offset, SEEK_SET);

    // Escreve o conteúdo do bitmap
    fwrite(bm->bits.data(), 1, bm->bits.size(), img);

    // Preenche o restante do(s) setor(es) com zeros caso sobre espaço
    uint32_t bytes_written = bm->bits.size();
    uint32_t bitmap_sector_bytes = bt->bitmap_size * BYTES_PER_SECTOR;

    if (bytes_written < bitmap_sector_bytes) {
        vector<uint8_t> zeros(bitmap_sector_bytes - bytes_written, 0);
        fwrite(zeros.data(), 1, zeros.size(), img);
    }
}

/*********************************** Função Principal ***********************************/
int main(int argc, char *argv[]) {
    if (argc != 2) {
        cout << "Uso: ./Formatador <Nome da imagem (.img)>" << endl;
        exit(1);
    }

    // Abrir o arquivo
    FILE *Img = fopen(argv[1], "r+b");
    if (!Img) {
        cout << "Erro: Arquivo " << argv[1] << " não pode ser aberto!" << endl;
        exit(1);
    }

    // Escreve o Boot Record no .img
    Boot_Record boot_record = fill_BT();
    write_boot_record(Img, &boot_record);

    // Escreve o Bitmap no .img
    Bitmap bitmap = Fill_System_Bitmap(&boot_record);
    write_bitmap(Img, &boot_record, &bitmap);

    cout << "Arquivo " << argv[1] << " formatado com sucesso!" << endl;

    return 0;
}
