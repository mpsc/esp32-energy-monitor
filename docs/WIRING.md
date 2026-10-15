# 🔌 Fiação e Segurança

## ⚠️ AVISO DE SEGURANCA

> **Este projeto conecta direto a rede eletrica (127V ou 220V).**
> Choque eletrico e risco de incendio. Regras basicas:
> - Trabalhe com a energia **desligada** no disjuntor durante a montagem
> - Use **fusivel** na linha de tensao (ex.: 250V/2A) e **fonte isolada** (certificada) para o ESP32
> - Nunca toque em circuito energizado; use protetor/involtip
> - O clamp CT e a parte **mais segura** (isolamento por inducao); o modulo ZMPT101B e a parte com risco
> - Teste primeiro com o clamp em **cabo fase individual** (nunca no cabo neutro+fase juntos)
> - Se nao tem experiencia com rede eletrica, peca ajuda a alguem qualificado

## Esquema geral

```
                    REDE 127/220V
                        │
        ┌───────────────┼────────────────────┐
        │               │                    │
     [FUSIVEL]     ZMPT101B IN           Clamp CT (SCT-013-030)
        │            │       │             volta ao cabo DEPOIS
        │          VCC   Saida AC          do clamp
        │         3.3V    │  │                  │
        │           │     │  │            saida 0..1V
        │           │    GND ┘                  │
        │           │                     divisor meio-tensao
        │           │                    100k/100k + 10uF p/ GND
        │           │                           │
        │        ┌──┴───────────────────────────┴──┐
        │        │  ESP32           GPIO34 ◄─ tensao (ZMPT)
        │        │                  GPIO34◄─ corrente (SCT)
        │        │  GND ── GND comum dos modulos     │
        │        └──────────────────────────────────┘
        │
     (neutro segue direto — o clamp NAO toca no neutro)
```

## Tabela de pinos

| Sinal | GPIO | Observacao |
|---|---|---|
| Tensao (ZMPT101B) | **34** | ADC1, input-only. Calibracao de fabrica via `analogReadMilliVolts` |
| Corrente (SCT-013-030) | **35** | ADC1, input-only. Saida 0-1V (30A full-scale) |
| Alimentacao modulos | 3V3 | **fonte isolada** (same GND do ESP32) |
| GND | GND | GND comum ESP32 + modulos |

> **Por que ADC1?** Com Wi-Fi ativo, o **ADC2 fica indisponivel**. So GPIO32-39 (ADC1) funcionam.
> GPIO34/35 nao tem pull-up/down interno — por isso o bias externo.

## SCT-013-030 vs SCT-013-000

| Versao | Saida | O que precisa |
|---|---|---|
| **SCT-013-030 (usado aqui)** | 0-1V para 30A | apenas **bias de meio-tensao** no ADC |
| SCT-013-000 | 0-50mA para 100A | burden (ex.: 33Ω) + bias + ganho |

### Bias de meio-tensao (obrigatorio pro SCT-030)

O ADC do ESP32 so le ate ~3.3V e nao aceita tensao negativa. A onda de saida do SCT oscila +/-1V; o divisor centraliza em 1.65V:

```
3V3 ── 100k ──┬── GPIO35
              ├── 10uF ── GND   (filtro)
SCT saida ── 100k ──┘
```

*(10k/100k tambem funciona; a referencia exata nao importa pois o firmware subtrai a media da janela.)*

## ZMPT101B (tensao)

- Alimentar com **3.3V isolado** (nao 5V! acima de 3.3V o modulo pode estourar o range do ADC)
- Entrada AC: **somente via resistor limitador / fusivel** conforme o datasheet do modulo
- Ajuste o potenciometro de sensibilidade do modulo para saida ~ +/-1V na tensao da rede — ou apenas calibre depois pelo software ([CALIBRATION.md](CALIBRATION.md))
- **Teste o ZMPT com multimetro antes** de ligar no ESP32

## Dicas praticas

- O clamp CT **nunca pode ser aberto** com a linha passando carga (vira um transformador em circuito aberto = picos de milhares de volts)
- Passe o clamp em **um unico cabo** (fase). No cabo de extensao/duas vias (fase+neutro juntos) os campos se cancelam e a leitura da **zero**
- Cabos finos de extensao genericos nao sao bons pra medição de corrente — use o cabo do fabricante do SCT
- Aceda o monitor por `http://192.168.4.1` (AP) ou `http://energia.local` (STA+mDNS)

## Depois de montar

Siga para [CALIBRATION.md](CALIBRATION.md) — sem calibracao os valores saem na proporcao errada.
