#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/arquivo.h>
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

/*
Retorna o bloco de numero block para a lista de blocos livres do sistema de arquivo sb
*/
int fs_put_block(struct superblock *sb, uint64_t block){
	//verifica o descritor do sistema de arquivos
	if(sb->magic != 0xdcc605f5){
		errno = EBADF;
		return -1;
	}

	//novo bloco a ser inserido na lista de blocos livres
	struct freepage *novoBloco = (struct freepage*) malloc (sb->blksz);

	//setando os ponteiros do bloco, a nova posição da tabela de blocos livres
	novoBloco->next = sb->freelist;
	sb->freelist = block;
	sb->freeblks++;

	//escrevendo os novos dados do super bloco (freelist e freeblks)
	lseek(sb->fd, 0, SEEK_SET);
	int aux = write(sb->fd, sb, sb->blksz);
	if(aux == -1){
		free(novoBloco);
		return -1;
	}

	//escrevendo novoBloco no arquivo
	lseek(sb->fd, block * sb->blksz, SEEK_SET);
	aux = write(sb->fd, novoBloco, sb->blksz);

	free(novoBloco);
	if(aux == -1) return -1;
	return 0;
}

/*
Escreve cnt bytes de buf no sistema de arquivos apontado por sb
*/
int fs_write_file(struct superblock *sb, const char *fname, char *buf, size_t cnt){
	int ii, aux;
	//verifica o descritor do sistema de arquivos
	if(sb->magic != 0xdcc605f5){
		errno = EBADF;
		return -1;
	}

	//verifica se o nome do arquivo (caminho) é maior que o permitido
	if(strlen(fname) > ((sb->blksz) - (8*sizeof(uint64_t)))){
		errno = ENAMETOOLONG;
		return -1;
	}

	uint64_t arquivoN, curr_n;
	uint64_t diretorioPai_n = find_block(sb,fname, 1);
	if(diretorioPai_n == 0) return -1; //por erro
	struct inode *diretorioPai = (struct inode*) calloc(sb->blksz,1);
	struct inode *arquivo = (struct inode*) calloc(sb->blksz,1);
	struct inode *auxin = (struct inode*) calloc(sb->blksz,1);
	struct nodeinfo *arquivoIn = (struct nodeinfo*) calloc(sb->blksz,1);
	struct nodeinfo *paiIn = (struct nodeinfo*) calloc(sb->blksz,1);

	//verifica se o arquivo existe no FS
	uint64_t arquivoAntigoN = find_block(sb, fname, 0);
	if(arquivoAntigoN > 0){
		if(fs_unlink(sb,fname) == -1){
			free(diretorioPai);
			free(arquivo);
			free(auxin);
			free(arquivoIn);
			free(paiIn);
			return -1;
		}
		arquivoN = fs_get_block(sb);
		if(arquivoN == (uint64_t)-1){
			free(diretorioPai);
			free(arquivo);
			free(auxin);
			free(arquivoIn);
			free(paiIn);
			errno = ENOSPC;
			return -1;
		}

		//procura pela referencia do arq no diretorio e atualiza para o novo arq
		curr_n = diretorioPai_n;
		do{
			lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
			aux = read(sb->fd, auxin, sb->blksz);
			for(ii=0; ii<NLINKS; ii++){
				if(auxin->links[ii] == arquivoAntigoN){
					auxin->links[ii] = arquivoN;
					lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
					aux = write(sb->fd, auxin, sb->blksz);
					break;
				}
			}
			curr_n = auxin->next;
		}
		while(curr_n != 0);
	}
	//se o arq nao existia
	else{
		//pega um novo bloco
		arquivoN = fs_get_block(sb);
		if(arquivoN == (uint64_t)-1){
			free(diretorioPai);
			free(arquivo);
			free(auxin);
			free(arquivoIn);
			free(paiIn);
			errno = ENOSPC;
			return -1;
		}

		//le o nodeinfo e inode do dir pai e atualiza o nodeinfo
		lseek(sb->fd, diretorioPai_n*sb->blksz, SEEK_SET);
		aux = read(sb->fd, diretorioPai, sb->blksz);
		lseek(sb->fd, diretorioPai->meta*sb->blksz, SEEK_SET);
		aux = read(sb->fd, paiIn, sb->blksz);
		paiIn->size++;
		lseek(sb->fd, diretorioPai->meta*sb->blksz, SEEK_SET);
		aux = write(sb->fd,paiIn,sb->blksz);

		//linka o novo bloco no dir pai
		link_block(sb,diretorioPai,diretorioPai_n,arquivoN);

		//escreve o inode do pai
		lseek(sb->fd, diretorioPai_n*sb->blksz, SEEK_SET);
		aux = write(sb->fd,diretorioPai,sb->blksz);
	}

	//cria estrutura do novo arq
	arquivo->parent = diretorioPai_n;
	arquivo->mode = IMREG;
	arquivo->next = 0;

	//pega novo bloco pro meta do arq
	arquivo->meta = fs_get_block(sb);
	if(arquivo->meta == (uint64_t)-1){
		free(diretorioPai);
		free(arquivo);
		free(auxin);
		free(arquivoIn);
		free(paiIn);
		errno = ENOSPC;
		return -1;
	}

	//cria estrutura do meta do arq e a escreve
	strcpy(arquivoIn->name,fname);
	arquivoIn->size = cnt; 
	lseek(sb->fd, arquivo->meta*sb->blksz, SEEK_SET);
	aux = write(sb->fd,arquivoIn,sb->blksz);

	//cria blocos e escreve o dado
	memset(auxin,0,sb->blksz);
	uint64_t block_n;
	uint64_t bytes_left = (uint64_t) cnt*sizeof(char);
	void *block = calloc(sb->blksz,1);

	uint64_t last_n;
	curr_n = arquivoN;
	free(auxin);
	auxin = arquivo;
	int flageof = 0;
	do{
		for(ii = 0; ii<NLINKS && !flageof; ii++){
			if(auxin->links[ii] == 0){
				//limpa o bloco
				memset(block,0,sb->blksz);
				if(bytes_left >= sb->blksz){
					memcpy(block,buf,sb->blksz);
					bytes_left -= sb->blksz;
				}
				else{
					memcpy(block,buf, bytes_left);
					flageof = 1;
				}
				//pega um indice pro bloco
				block_n = fs_get_block(sb);
				if(block_n == (uint64_t) -1){
					free(diretorioPai);
					free(arquivo);
					free(arquivoIn);
					free(paiIn);
					errno = ENOSPC;
					return -1;
				}

				//linka o bloco
				auxin->links[ii] = block_n;

				//escreve o bloco
				lseek(sb->fd, block_n*sb->blksz, SEEK_SET);
				aux = write(sb->fd,block,sb->blksz);
			}
		}
		if(flageof){
			//Escreve o inode corrente
			lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
			aux = write(sb->fd,auxin,sb->blksz);
		}
		else{
			//Inode cheio, e eof n encontrado

			last_n = curr_n;
			auxin->next = fs_get_block(sb);
			if(auxin->next == (uint64_t) -1){
				free(diretorioPai);
				free(arquivo);
				free(arquivoIn);
				free(paiIn);
				errno = ENOSPC;
				return -1;
			}

			//Escreve o inode corrente
			lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
			aux = write(sb->fd,auxin,sb->blksz);

			//Limpa o struct auxin e atualiza curr_n
			curr_n = auxin->next;
			memset(auxin,0,sb->blksz);

			//Cria estrutura do inode filho
			auxin->parent = arquivoN;
			auxin->meta = last_n;
			auxin->mode = IMCHILD;
			auxin->next = 0;
		}
	}
	while(!flageof);

	free(diretorioPai);
	free(arquivo);
	free(arquivoIn);
	free(paiIn);

	return 0;
}