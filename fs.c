#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <string.h>
#include <unistd.h>

#include "fs.h"

/*
Constroi um novo sistema de arquivos no arquivo de nome fname
*/
struct superblock * fs_format(const char *fname, uint64_t blocksize)
{
	//NLINKS = (blocksize - (4 * sizeof(uint64_t)))/sizeof(uint64_t);

	//verifica se o tamanho do bloco eh maior que o minimo
	if(blocksize < MIN_BLOCK_SIZE){
		errno = EINVAL;
		return NULL;
	}

	//calcula o tamanho de fname.
	FILE* arquivo = fopen(fname, "r");
	long fsize;
	if(arquivo != NULL){
		fseek(arquivo, 0, SEEK_END);
		fsize = ftell(arquivo);
	}
	fclose(arquivo);

	//calcula numero de blocos
	int numeroBlocos = fsize / blocksize;

	//verifica se o numero de blocos eh maior que o minimo
	if(numeroBlocos < MIN_BLOCK_COUNT){
		errno = ENOSPC;
		return NULL;
	}

	//criando o superbloco
	struct superblock* superBloco = (struct superblock*) malloc (blocksize);
	superBloco->magic = 0xdcc605f5; //conforme estabelecido em fs.h
	superBloco->blks = numeroBlocos; 
	superBloco->blksz = blocksize;

	//superbloco, nodeinfo, root e inode de root ocupam 3 blocos
	int memoriaOcupada = 3

	//blocos livres
	superBloco->freeblks = numeroBlocos-memoriaOcupada;

	//apontador para o primeiro bloco livre
	superBloco->freelist = 3;

	//apontador para o inode da pasta raiz
	superBloco->root = 2;

	//descritor de arquivos
	superBloco->descritorArquivos = open(fname, O_RDWR, S_IWRITE | S_IREAD);
	if(superBloco->descritorArquivos == -1){
		errno = EBADF;
		free(superBloco);
		return NULL;
	}

	//inicializando o superbloco
	int aux = write(superBloco->descritorArquivos, superBloco, superBloco->blksz);
	if(aux == -1){
		close(superBloco->descritorArquivos);
		free(superBloco);
		return NULL;
	}

	//inicializando a pasta raiz
	struct nodeinfo* rootInfo = (struct nodeinfo*) calloc (superBloco->blksz,1);
	rootInfo->size = 0;
	strcpy(rootInfo->name, "/\0");
	aux = write(superBloco->descritorArquivos, rootInfo, superBloco->blksz);
	free(rootInfo);

	struct inode* rootInode = (struct inode*) calloc (superBloco->blksz,1);
	rootInode->mode = IMDIR;
	rootInode->parent = 0;
	rootInode->meta = 1;
	rootInode->next = 0;
	aux = write(superBloco->descritorArquivos, rootInode, superBloco->blksz);
	free(rootInode);

	//inicializando lista de blocos vazios
	struct freepage* root_fp = (struct freepage*) malloc (sizeof(struct freepage*));
	for(int i = superBloco->freelist; i < superBloco->blks; i++){
		if(i == (superBloco->blks - 1)){
			root_fp->next = 0; //ultima pagina livre
		}
		else{
			root_fp->next = i+1;
		}

		aux = write(superBloco->descritorArquivos, root_fp, superBloco->blksz);
	}
	free(root_fp);

	return superBloco;
}

/*
Abre o sistema de arquivos em fname e retorna seu superbloco
*/
struct superblock * fs_open(const char *fname){
	//pega o descritor de arquivo do FS
	int descritorArquivos = open(fname, O_RDWR);

	// aplica uma trava exclusiva no arquivo (LOCK_EX = exclusive lock)
	// apenas um processo poderá usar esse arquivo de cada vez
	// flock retorna 0 se sucesso, -1 se erro
	if((flock(descritorArquivos, LOCK_EX | LOCK_NB)) == -1){
		errno = EBUSY;
		close(descritorArquivos);
		return NULL;
	}

	//move o leitor para a posição inicial do arquivo.
	lseek(descritorArquivos, 0, SEEK_SET);

	//carrega o superbloco do FS
	struct superblock* superbloco = (struct superblock*) malloc(sizeof(struct superblock));
	if(read(descritorArquivos, superbloco, sizeof(struct superblock)) == -1 ){
		close(descritorArquivos);
		free(superbloco);
		return NULL;
	}


	//verifica o erro EBADF
	if(superbloco->magic != 0xdcc605f5){
		//LOCK_UN: remove a trava do arquivo
		flock(descritorArquivos, LOCK_UN | LOCK_NB);
		close(descritorArquivos);
		errno = EBADF;
		free(superbloco);
		return NULL;
	}

	return superbloco;
}

/* 
Fecha o sistema de arquivos apontado por sb
 */
int fs_close(struct superblock *sb){
	//verifica o descritor do sistema de arquivos
	if(sb->magic != 0xdcc605f5){
		errno = EBADF;
		return -1;
	}

	//LOCK_UN: remove a trava do arquivo
	if(flock(sb->fd, LOCK_UN | LOCK_NB) == -1){
		errno = EBUSY;
		return -1;
	}

	//fechando o arquivo
	int aux = close(sb->fd);
	if(aux == -1) return -1;
	free(sb);

	return 0;
}

/*
Pega um ponteiro para um bloco livre no sistema de arquivos sb
*/
uint64_t fs_get_block(struct superblock *sb){
	//verifica o descritor do sistema de arquivos
	if(sb->magic != 0xdcc605f5){
		errno = EBADF;
		return (uint64_t) 0;
	}

	//verifica se ha blocos livres
	if(sb->freeblks == 0){
		errno = ENOSPC;
		return (uint64_t) 0;
	}

	struct freepage *pagina = (struct freepage*) calloc (sb->blksz,1);
	//localizando posição do primeiro bloco livre
	lseek(sb->fd, (sb->freelist * sb->blksz), SEEK_SET);
	//verificando se há algum erro na leitura
	int aux = read(sb->fd, pagina, sb->blksz);
	if(aux == -1){
		free(pagina);
		return (uint64_t) 0;
	}

	//pegando o "ponteiro" do bloco a ser retornado
	uint64_t bloco = sb->freelist;
	//mudando o ponteiro de lista vazia para o próximo bloco livre
	sb->freelist = pagina->next;
	//decrementando a quantidade de blocos livres
	sb->freeblks--;

	//escrevendo os novos dados do super bloco (freelist e freeblks)
	lseek(sb->fd, 0, SEEK_SET);
	aux = write(sb->fd, sb, sb->blksz);
	if(aux == -1){
		free(pagina);
		return (uint64_t) 0;
	}

	free(pagina);
	return bloco;
}
