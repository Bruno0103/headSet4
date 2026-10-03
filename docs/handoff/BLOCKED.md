# Bloqueio do Agente 0

**O que:**
Não é possível verificar as APIs do ESP-IDF v6.0.2 nos headers físicos (`$IDF_PATH/components/...`) para construir o `docs/API_VERIFIED.md` com total certeza e resolver os itens `[VERIFICAR]` do `FINDINGS.md`.

**Por quê:**
A variável de ambiente `$IDF_PATH` não está definida neste ambiente e eu não possuo acesso de leitura aos diretórios de instalação padrão do ESP-IDF (ex: `C:\Users\Bruno\esp\esp-idf`) devido a restrições de sandbox e permissão do sistema local de arquivos, o que impossibilita buscar a documentação e os headers exatos da versão instalada (6.0.2).

**Opções:**
1. **Pular validação estrita:** Eu posso preencher o `API_VERIFIED.md` e os arquivos de arquitetura usando meu conhecimento interno sobre o ESP-IDF (versões 5.x/6.x), marcando o que for incerto como "NÃO VERIFICADO". 
2. **Fornecer o caminho do IDF:** Você pode apontar o caminho exato do IDF ou executar um comando para copiar os headers de `components/bt` e `components/driver` para uma pasta dentro deste repositório para que eu possa lê-los.
3. **Pular o Agente 0:** Se esses documentos já estiverem definidos de alguma outra forma ou se quiser assumir o risco e avançar diretamente para os próximos agentes de código, basta me avisar.
