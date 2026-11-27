# Guide de l'utilisateur

`docs/TWIST-Console-Guide-utilisateur.docx` and `.pdf` are made from
`build.js` (the text, in French) and the screenshots of the application.
To make them again after a change of the program or of the text:

```sh
make && cd app && dotnet build -c Release && cd ..
cp libtwcore.so libtwist.so libopera.so libpi4.so app/Twist.Screenshots/bin/Release/net8.0/
./tests/gen_test_wav F5ZZO,TWS:F6FBB,TWL:F1HDI:JN18cs 0.5 -18 1520 1.5 120 12000 /tmp/tw_t1.wav
# all the settings tabs: TWIST_ALL_TABS=1; the waterfall of the replay in real time: "realtime"
TWIST_ALL_TABS=1 dotnet app/Twist.Screenshots/bin/Release/net8.0/Twist.Screenshots.dll /tmp/shots /tmp/tw_t1.wav realtime
node docs/guide/build.js /tmp/shots docs           # npm install docx, if missing
python3 docs/guide/topdf.py docs/TWIST-Console-Guide-utilisateur.docx   # LibreOffice
```

The .docx keeps its table of contents as a field, filled by Word when
the file is opened (it asks to update the fields: Oui); `topdf.py`
fills it before writing the PDF.
