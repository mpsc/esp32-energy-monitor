# 🏠 Home Assistant (MQTT + Auto-Discovery)

O ESP32 publica automaticamente os sensores no Home Assistant via **MQTT Discovery** — nada de copiar/colar YAML.

## Requisitos

1. Um broker MQTT rodando (o **Mosquitto** ja vem junto no addon do HA, ou qualquer outro)
2. Credenciais de um usuario MQTT

## Configuracao (pelo dashboard)

1. **Configuracoes → MQTT** no dashboard do ESP32
2. Preencha:
   - **Host**: IP do broker (ex.: `192.168.0.10`) ou `homeassistant.local`
   - **Porta**: `1883` (padrao)
   - **Usuario/Senha**: do broker
   - **Topico base**: `energia` (padrao — mude se tiver varios monitores)
   - ☑ **Auto-discovery Home Assistant**: marcado por padrao
3. *Salvar e reiniciar*

Pronto: ao conectar, o HA cria o dispositivo **"Monitor de Energia"** com 8 entidades.

## Entidades criadas

| Entidade | Unidade | Classe | Uso no HA |
|---|---|---|---|
| Potencia | W | `power` | gauge, graficos, automacoes |
| Tensao | V | `voltage` | monitoramento da rede |
| Corrente | A | `current` | - |
| Fator de Potencia | % | `power_factor` | qualidade de carga |
| Energia Total | kWh | `energy` / `total_increasing` | **painel Energia** |
| Energia Hora Pico | kWh | `energy` / `total_increasing` | painel Energia (tarifa) |
| Energia Fora Pico | kWh | `energy` / `total_increasing` | painel Energia (tarifa) |
| Custo Estimado | R$ | - | informativo |

## Painel de Energia do HA

1. HA → **Configuracoes → Dashboards → Energia**
2. Adicione **Consumo da rede** e selecione `sensor.energia_energia_total`
3. (Opcional) adicione os sensores de pico/fora-pico como outras fontes de tarifa

> `state_class: total_increasing` e obrigatorio pra funcionar — ja esta nos payloads de discovery. O **Reset** pela pagina zera o contador (o HA trata a queda como troca de medidor).

## Topicos (base `energia`)

| Topico | Payload | Retido | Observacao |
|---|---|---|---|
| `energia/availability` | `online`/`offline` | ✔ | LWT — offline se o ESP32 cair |
| `energia/state` | JSON completo | ✘ | Node-RED / automacoes |
| `energia/power` | `423.5` | ✔ | W |
| `energia/voltage` | `126.4` | ✔ | V |
| `energia/current` | `3.350` | ✔ | A |
| `energia/power_factor` | `96.2` | ✔ | % (0-100) |
| `energia/energy` | `12.3456` | ✔ | kWh total |
| `energia/energy_peak` | `1.2345` | ✔ | kWh hora pico |
| `energia/energy_off` | `11.1111` | ✔ | kWh fora do pico |
| `energia/cost` | `10.23` | ✔ | R$ acumulado |

Discovery publicado em `homeassistant/sensor/<base>/<chave>/config` (JSON retido, prefixo configuravel no `config.h`).

## JSON agregado (Node-RED)

```json
{ "p": 423.5, "v": 126.4, "i": 3.35, "pf": 0.96,
  "kt": 12.345, "kp": 1.234, "ko": 11.111, "cost": 10.23 }
```

Topico: `energia/state`. Injete direto num `json` node e divirta-se.

## Multiplos monitores

Cada ESP32 deve usar um **topico base diferente** (`energia-sala`, `energia-geladeira`...). Os `unique_id` tambem diferem por dispositivo (`energia_power` usa o base — se duplicar, mude o base).

## Solucao de problemas

| Sintoma | Causa provavel |
|---|---|
| Sensores nao aparecem no HA | Auto-discovery desmarcado, ou prefixo diferente do configurado no HA (`homeassistant` e o padrao) |
| Dispositivo "offline" no HA | Broker inacessivel do ESP32 (teste ping/IP); cheque usuario/senha |
| Valores congelados | ESP32 reiniciou — o estado retido mostra o ultimo valor; o `availability` deve ir a `offline` (o HA mostra "indisponivel") |
| TLS | Nao suportado na v1 (roadmap). Use broker local ou tunel |

## Exemplo de automatizacao

```yaml
automation:
  - alias: "Alerta de consumo alto"
    trigger:
      platform: numeric_state
      entity_id: sensor.energia_power
      above: 3000        # 3kW
    action:
      service: notify.mobile_app
      data:
        message: "Consumo alto: {{ states('sensor.energia_power') }} W"
```
