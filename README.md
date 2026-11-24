# Lixeira Inteligente

Protótipo de Internet das Coisas (IoT) para monitorar o nível de ocupação de lixeiras e notificar automaticamente a equipe de coleta quando uma lixeira está cheia.

Projeto da disciplina **Objetos Inteligentes Conectados** — Universidade Presbiteriana Mackenzie.

- **Vídeo de demonstração:**https://youtu.be/b9cXbZGETeo
- **Integrantes:** Gabriel Freitas, Lucas Ricci, Melissa Zanelato Lupinacci e Moisés Manoel Santos
- **Agenda 2030 da ONU:** ODS 11 (Cidades e Comunidades Sustentáveis), meta 11.6, que trata de reduzir o impacto ambiental negativo das cidades, com atenção especial à gestão de resíduos municipais.

## Dashboard

![Dashboard de monitoramento no Grafana](imagens/dashboard-grafana.png)

Cartões com o total de lixeiras e a situação (normal, atenção, cheia), um medidor por lixeira, ocupação média, ranking, histórico do nível, últimas leituras críticas, usos por abertura de tampa e o painel da meta 11.6 da Agenda 2030.

## Descrição

Cada lixeira é um dispositivo **ESP32** com três componentes:

| Componente | Tipo | Função | Pino |
|---|---|---|---|
| Sensor ultrassônico HC-SR04 | sensor | mede a distância até os resíduos (nível de ocupação) | TRIG 5, ECHO 18 |
| Sensor de presença PIR | sensor | detecta a aproximação de uma pessoa | 27 |
| Servomotor SG90 | atuador | abre e fecha a tampa | 13 |

O mesmo modelo de dispositivo é replicado em **3 lixeiras** (`lixeira01`, `lixeira02`, `lixeira03`). O projeto é simulado na plataforma [Wokwi](https://wokwi.com).

## Arquitetura

![Topologia lógica da aplicação](imagens/topologia.png)


```
ESP32 (Wokwi) --MQTT--> broker.hivemq.com --MQTT--> Node-RED --> InfluxDB Cloud --> Grafana Cloud
                                                       |--> API do Telegram (alertas)
                                                       '--> comando "abrir" --> ESP32 (servo)
```

Regras de negócio implementadas no Node-RED:

1. Classifica o nível: normal (abaixo de 60%), atenção (de 60% a 84%) e cheia (85% ou mais).
2. Envia uma única notificação no Telegram quando a lixeira fica cheia; a regra é rearmada quando o nível cai abaixo de 60%.
3. Ao receber um evento de presença, envia o comando de abertura da tampa, exceto se o último nível conhecido for igual ou superior a 95%.
4. Grava a telemetria e os eventos de presença no InfluxDB.
5. Avisa no Telegram se um dispositivo ficar offline (Last Will do MQTT).

Os limiares são parâmetros definidos pelo grupo e podem ser ajustados no código.

## Imagens do projeto

**Diagrama de conexão do dispositivo (replicado em cada lixeira)**

![Diagrama de conexão do ESP32 com PIR, HC-SR04 e servo](imagens/diagrama-conexao.png)

**Simulação no Wokwi**

![Simulação do dispositivo no Wokwi](imagens/wokwi-simulacao.png)

**Fluxo do Node-RED**

![Fluxo do Node-RED](imagens/node-red.png)

**Dados gravados no InfluxDB (Data Explorer)**

![Consulta SQL no InfluxDB](imagens/influxdb-data-explorer.png)

## Estrutura do repositório

```
firmware/    sketch.ino, diagram.json, libraries.txt   (projeto Wokwi do dispositivo)
node-red/    flows.json                                (fluxo do Node-RED)
grafana/     dashboard-lixeira-inteligente.json        (dashboard do Grafana)
imagens/     capturas de tela e diagramas usados neste README
```

## Tópicos MQTT

Prefixo: `upm/ioc/lixeira/{id}`

| Tópico | Sentido | Conteúdo |
|---|---|---|
| `/telemetria` | dispositivo para Node-RED | JSON com distancia_cm, nivel_pct, tampa e rssi (a cada 5 s) |
| `/presenca` | dispositivo para Node-RED | JSON com o evento de presença |
| `/cmd/tampa` | Node-RED para dispositivo | texto `abrir` ou `fechar` |
| `/status` | dispositivo para Node-RED | `online` ou `offline` (retido) |

## Como reproduzir

### 1. Dispositivos (Wokwi)
1. Em https://wokwi.com crie um projeto **ESP32**.
2. Substitua o conteúdo de `sketch.ino`, `diagram.json` e `libraries.txt` pelos arquivos da pasta `firmware`.
3. Rode a simulação. O Serial Monitor deve mostrar `[wifi] conectado` e `[mqtt] conectado`.
4. Para a lixeira 2 e a 3, salve uma cópia do projeto e altere **somente** a constante `BIN_ID` (`lixeira02`, `lixeira03`).

### 2. InfluxDB Cloud
1. Crie uma conta gratuita em https://cloud2.influxdata.com.
2. Crie um bucket chamado `lixeiras`.
3. Crie um token de API com permissão de leitura e escrita nesse bucket.

### 3. Telegram
1. No Telegram, crie um bot com o **@BotFather** e guarde o token.
2. Envie `/start` para o seu bot.
3. Descubra o seu chat ID com o **@userinfobot**.

### 4. Node-RED
1. Instale o Node-RED e as paletas `node-red-contrib-influxdb` e `node-red-contrib-telegrambot`.
2. Importe o arquivo `node-red/flows.json` (menu, Importar).
3. No nó **Telegram bot**, informe o token do seu bot.
4. Nas variáveis de ambiente do fluxo, troque `SEU_CHAT_ID` pelo seu chat ID.
5. No nó **InfluxDB Cloud**, informe a URL, o token, a organização (`SUA_ORG`) e o bucket `lixeiras`.
6. Clique em Implementar. O nó **Teste: lixeira cheia** dispara um alerta de teste.

### 5. Grafana Cloud
1. Crie uma conta gratuita em https://grafana.com.
2. Adicione um data source **InfluxDB** com a linguagem **SQL**, a URL do seu InfluxDB, o database `lixeiras` e o token.
3. Importe `grafana/dashboard-lixeira-inteligente.json` (Dashboards, New, Import).
4. Se necessário, troque o `uid` do data source no arquivo pelo `uid` do seu data source.

## Observações

- **Dados sensíveis:** os arquivos deste repositório não contêm tokens, chat ID nem senhas. Os valores `SEU_CHAT_ID`, `SUA_ORG` e `SEU-HOST` são marcadores que devem ser substituídos.
- **Broker público:** o broker HiveMQ público não tem autenticação, então qualquer pessoa pode assinar os tópicos. Por isso o prefixo dos tópicos é exclusivo do grupo e nenhum dado sensível é enviado.
- **Conectividade:** no simulador a conexão é Wi-Fi. Em uma implantação real, com lixeiras espalhadas pela cidade, a tecnologia recomendada seria LoRaWAN.
- **Hardware real:** o pino ECHO do HC-SR04 trabalha em 5 V e exige divisor de tensão para ser ligado ao ESP32 (3,3 V).
