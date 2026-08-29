# Provera teze — pokrenuti posle SVAKE izmene teksta.
#   powershell -File teza\provera.ps1
#
# Sve tri greske koje ovo trazi prolaze LaTeX prevod bez ijedne prijave.
# Vide se jedino ovako ili citanjem gotovog PDF-a.

$tex = Join-Path $PSScriptRoot "teza.tex"
$log = Join-Path $PSScriptRoot "teza.log"
$pdf = Join-Path $PSScriptRoot "teza.pdf"

if (-not (Test-Path $tex)) { Write-Output "Nema teza.tex — jos nije napravljen."; exit }

$t = Get-Content $tex -Raw -Encoding UTF8
$greske = 0

Write-Output "==================== PROVERA TEZE ===================="

# 1. Homoglifi — cirilicno slovo neposredno uz latinicno, u oba smera.
#    Najcesca posledica rucnog preformulisanja i kopiranja iz drugog izvora.
$h = [regex]::Matches($t, '[\u0400-\u04FF][a-zA-Z]|[a-zA-Z][\u0400-\u04FF]')
Write-Output ("homoglifi (mesana pisma u istoj reci) : " + $h.Count)
if ($h.Count -gt 0) {
    $greske++
    $lines = Get-Content $tex -Encoding UTF8
    for ($i=0; $i -lt $lines.Count; $i++) {
        $m = [regex]::Matches($lines[$i], '[\u0400-\u04FF][a-zA-Z]|[a-zA-Z][\u0400-\u04FF]')
        if ($m.Count -gt 0) { Write-Output ("   linija " + ($i+1) + ": " + $lines[$i].Trim()) }
    }
}

# 2. Osakacene komande — heredoc i slicni alati pojedu obrnutu kosu crtu.
#    LaTeX tada ne vidi komandu, pa je NE prijavljuje kao gresku.
$o = [regex]::Matches($t, '(?<![\\A-Za-z])(ef|abel|ite|extbf|extit|ection|hapter|egin|nd|ode|aption|able|mph)\{')
Write-Output ("osakacene komande (ef{, abel{, ...)   : " + $o.Count)
if ($o.Count -gt 0) { $greske++; $o | Select-Object -First 10 | ForEach-Object { Write-Output ("   " + $_.Value) } }

# 3. Mesani navodnici — otvoren Unicode, zatvoren ASCII. csquotes puca tek
#    na kraju dokumenta, sa brojem linije koji ne pokazuje uzrok.
$n = [regex]::Matches($t, '[\u201E\u201C][^\u201C\u201D\r\n]{0,200}"')
Write-Output ("mesani navodnici                      : " + $n.Count)
if ($n.Count -gt 0) { $greske++; $n | Select-Object -First 5 | ForEach-Object { Write-Output ("   " + $_.Value.Substring(0,[Math]::Min(70,$_.Value.Length))) } }

# 4. Zaostali markeri
$m2 = [regex]::Matches($t, 'ТРЕБА ПОДАТАК|ТРЕБА ПРОВЕРА|АУТОР:|TODO|FIXME')
Write-Output ("markeri i TODO                        : " + $m2.Count)
if ($m2.Count -gt 0) { $greske++ }

# 5. Interna projektna imena — ne smeju u tehnicki tekst javnog dokumenta.
#    Biografija autora je izuzeta, odlukom autora 2026-08-26: u njoj sme da stoji
#    ime firme u kojoj radi, jer je to podatak o njemu a ne o platformi na kojoj je
#    prototip radjen. Pravilo i dalje vazi za ceo ostatak rada.
$tBezBio = [regex]::Replace($t, '(?s)\\begin\{biografija\}.*?\\end\{biografija\}', '')
$i2 = [regex]::Matches($tBezBio, '(?i)itk-|itk_|QDC017|IKOTEK|Quectel|Versa')
Write-Output ("interna projektna imena               : " + $i2.Count)
if ($i2.Count -gt 0) { $greske++; $i2 | Select-Object -First 5 | ForEach-Object { Write-Output ("   " + $_.Value) } }

# 6. Duga crta u tekstu — autor je 2026-08-25 odlucio da u celom radu ide kratka crtica.
#    Komentari se preskacu: ne stampaju se, pa nemaju veze sa izgledom rada.
$redovi = Get-Content $tex -Encoding UTF8
$dc = @()
for ($i = 0; $i -lt $redovi.Count; $i++) {
    if ($redovi[$i].TrimStart().StartsWith('%')) { continue }
    if ($redovi[$i] -match '—') { $dc += ("   linija " + ($i+1) + ": " + $redovi[$i].Trim()) }
}
Write-Output ("duga crta u tekstu                    : " + $dc.Count)
if ($dc.Count -gt 0) { $greske++; $dc | Select-Object -First 5 | ForEach-Object { Write-Output $_ } }

# 7. Stanje prevoda, ako log postoji
if (Test-Path $log) {
    Write-Output "---- prevod ----"
    foreach ($p in @('Missing character','^!','Overfull','undefined')) {
        $c = (Select-String -Path $log -Pattern $p | Measure-Object).Count
        Write-Output ("{0,-38}: {1}" -f $p, $c)
        if ($c -gt 0) { $greske++ }
    }
}

if (Test-Path $pdf) {
    $pdfinfo = "C:\Users\Dida\AppData\Local\Programs\MiKTeX\miktex\bin\x64\pdfinfo.exe"
    if (Test-Path $pdfinfo) { & $pdfinfo $pdf | Select-String "^Pages" }
}

Write-Output "====================================================="
if ($greske -eq 0) { Write-Output "CISTO." } else { Write-Output ("PAZNJA: " + $greske + " vrsta problema — pogledaj gore.") }
