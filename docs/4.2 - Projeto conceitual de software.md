# Projeto Conceitual de Software

Expliquem textualmente o algoritmo desenvolvido, o que **NÃO CONSISTE** em simplesmente replicar o código, e sim em explicar como ele funciona, justificando suas escolhas de projeto. Tópicos importantes a serem descritos incluem:

- **Coleta de dados:** conexão com sensores, definição da taxa de amostragem etc.
- **Processamento de dados:** filtro média-móvel, detecção de faces, reconhecimento de caracteres etc. 
- **Atuadores:** PWM, escrita em _drivers_ etc. 
- **Armazenamento/transmissão:** salvar em arquivo, enviar para a nuvem etc.   
- **Interface com o usuário:** GUI, interrupções para botões etc.
- **Inserção do programa no sistema operacional:** inicialização automática (_crontab_, ```/etc/init.d```), desligamento de serviços desnecessários, _Buildroot_ etc.

Além disso, utilizem fluxogramas como o da figura abaixo para auxiliar no entendimento do algoritmo desenvolvido.

![Fluxograma](https://raw.githubusercontent.com/DiogoCaetanoGarcia/SOE_template/refs/heads/main/docs/figs/exemplo_de_fluxograma.png)
**Figura 1.** Exemplo de fluxograma [1].

## Referências

1. G. Asquel e T. da Cunha, _Desenvolvimento e avaliação de uma tocha de soldagem inteligente_. Soldagem & Inspeção, vol. 24, 2019.
