# 💿 Sistema de Arquivos CBFS (Contiguous Bitmap File System)

> Implementação de um sistema de arquivos baseado em alocação contígua e gerenciamento via Bitmap, desenvolvido em C++ para a disciplina de Sistemas Operacionais.

![Badge C++](https://img.shields.io/badge/Language-C++-00599C?logo=c%2B%2B&logoColor=white)
![Badge OS](https://img.shields.io/badge/Topic-Operating%20Systems-black?logo=linux&logoColor=white)
![Badge Academic](https://img.shields.io/badge/Type-Academic%20Project-blue)

## 🏫 Sobre o Projeto

Este projeto implementa o **CBFS**, um sistema de arquivos didático que opera sobre um arquivo binário (disco virtual). Ele gerencia a persistência de dados utilizando estruturas de baixo nível definidas manualmente.

### Características Técnicas
* **Boot Record:** Setor inicial contendo metadados (Assinatura `0x7777`, ponteiros para Bitmap e Root).
* **Alocação Contígua:** Arquivos são armazenados em setores sequenciais para simplicidade de leitura.
* **Bitmap de Espaço Livre:** Mapa de bits para rastrear setores ocupados e livres.
* **Diretório Raiz:** Tabela fixa com capacidade para até 128 arquivos.
* **Área de Dados:** Região onde são armazenados os dados dos arquivos.

## 🚀 Funcionalidades

O sistema opera via linha de comando (CLI) e suporta as seguintes operações:

* `formatar`: Inicializa o disco, criando o Boot Record, zerando o Bitmap e o Diretório Raiz.
* `listar`: Exibe os arquivos armazenados (Nome, Extensão, Tamanho e LBA Inicial).
* `importar`: Copia um arquivo do computador (Host) para o disco virtual CBFS.
* `exportar`: Extrai um arquivo do disco virtual CBFS para o computador.
* `remover`: Exclusão lógica (marca como excluído e libera os bits no mapa).
* `status`: Exibe um mapa visual da ocupação do disco e estatísticas de uso.

## 📂 Estrutura do Código

O projeto é contido em um único arquivo fonte para facilidade de compilação:

```bash
Sistema-de-Arquivos/
├── src/
│   └── CBFS.cpp     # Código fonte completo (Estruturas + Lógica)
└── README.md
```

## 🛠️ Como Compilar e Executar

Você precisará de um compilador C++ (G++).

### 1. Compilar
```bash
g++ CBFS.cpp -o cbfs
```

### 2. Usar o Sistema

As operações seguem a sintaxe: `./cbfs <comando> <arquivo_de_disco> [argumentos]`

#### Formatar um novo disco (ex: 1000 setores)
```bash
./cbfs formatar disco.img 1000
```

#### Importar um arquivo do PC
```bash
./cbfs importar disco.img imagem.png
```

#### Listar arquivos no disco
```bash
./cbfs listar disco.img
```

#### Ver o mapa de ocupação
```bash
./cbfs status disco.img
```

#### Exportar um arquivo de volta para o PC
```bash
./cbfs exportar disco.img IMAGEM.PNG
```

#### Remover um arquivo
```bash
./cbfs remover disco.img IMAGEM.PNG
```

## 🧠 Detalhes da Implementação

A estrutura do disco virtual segue a ordem:
1.  **Boot Record (1 Setor):** Cabeçalho do sistema.
2.  **Bitmap:** $N$ setores (calculado com base no tamanho total).
3.  **Diretório Raiz (8 Setores):** Metadados dos arquivos.
4.  **Área de Dados:** Espaço restante para conteúdo dos arquivos.
