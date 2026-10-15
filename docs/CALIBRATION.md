# 🎯 Calibracao

Os fatores `calV` e `calI` corrigem a diferenca entre o valor medido pelo ESP32 e o valor real. Formula:

```
cal = valor_real / valor_medido
```

Calibre **tensao primeiro**, depois **corrente**.

## Passo 1 — Calibrar tensao (calV)

1. Meça a tensao da sua rede com um **multimetro** (ex.: 126.4V)
2. Abra o dashboard → **Configuracoes → Calibracao**
3. Observe o valor de `Tensao` (V) exibido no topo (ex.: mediu 298.5V com o clamp sem carga → sai um numero aleatorio; calibre com o modulo ligado e sem carga na rede)
4. Calcule: `calV = 126.4 / 298.5 = 0.423`
5. Salve (botao *Salvar e reiniciar*)

> Dica: os **offsets ao vivo** (mV) mostrados na calibracao devem ficar proximos de ~1650mV (= 3.3V/2, meio-tensao). Se estiverem fora disso, revise o divisor/bias.

## Passo 2 — Calibrar corrente (calI)

1. Ligue uma **carga conhecida** (ex.: chaleira de 1500W, ou use um multimetro em serie)
2. Espere ~30s e anote o `Corrente` (A) exibido (ex.: 29.8A — sem calibracao, 30A cheios = 1V na saida)
3. Calcule: `calI = carga_real / (tensao_real × I_medido)`
   - Ex.: chaleira 1500W em 126.4V → 11.87A reais; display mostrou 11.5A
   - `calI = 11.87 / 11.5 = 1.032`
4. Salve e confira tambem a `Potencia` (W) ≈ 1500W e o `Fator de potencia` (0.95-1.00 pra carga resistiva)

## Passo 3 — Verifique o fator de potencia

- Carga resistiva (chaleira, lampada incandescente): PF deve ficar **> 0.95**
- Se o PF sair muito baixo ou a potencia fluctuar muito, confira o alinhamento temporal: as leituras de V e I sao sequenciais (~25µs de diferenca) — em 60Hz isso e desprezivel, mas **CT de baixa qualidade** podem introduzir erro de fase. Prefira o SCT-013 original/certificado.

## Onde ficam salvos

`calV`/`calI` persistem na NVS (`prefs "cfg"`) e valem para:
- Dashboard web
- MQTT (valores publicados)
- Acumulacao de kWh (custo correto)

## Recalibrando

Basta repetir o procedimento a qualquer momento — os valores atuais ficam preenchidos no formulario.
