-- IRQ4 service envelope and $14856 caller timing. Captures stay ignored.
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=main.spaces.program
local board=manager.machine.devices[":mainpcb"]
local timer=emu.item(assert(board.items["0/m_timer_irq_state"]))
local blank=emu.item(assert(board.items["0/m_vblank_irq_state"]))
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write("AB2IRQ46")
local returns
if os.getenv("AB2_ORACLE_IRQ4_RETURN")=="1" then
 returns=assert(io.open(os.getenv("AB2_ORACLE_OUT")..".returns","wb"));returns:write("AB2RTE42")
end
local initial_usp,return_pending,return_entry
local guard,active,last,hook=false,false,nil,nil
local events,hooks,subevents,pre={},{},{},nil
local gameflow_pcs,window_gameflow_pcs={},{}
local frame,calls,dropped,operand_frontier=0,0,0,0
local limit=tonumber(os.getenv("AB2_ORACLE_PASSES") or "600")
local directed=os.getenv("AB2_ORACLE_IRQ4_DIRECTED")=="1"
-- Data/RMW instruction lengths; extensions and prefetches are clock phases.
local operand_lengths={
 [0xb84c]=4,[0xb854]=8,[0xb860]=4,[0xb876]=4,[0xb87c]=4,[0xb880]=4,[0xcfc4]=8,[0xcfd0]=8,
 [0xcfdc]=4,[0xcfe0]=6,[0xcfea]=6,[0x13e5c]=8,[0x13e6c]=4,[0x13e70]=4,[0x13e80]=4,[0x13e84]=6,
 [0x13e8e]=8,[0x13e96]=6,[0x13e9c]=4,[0x13ea0]=8,[0x13f92]=6,[0x13f9c]=4,[0x13fa8]=4,[0x13fb6]=4,
 [0x13fba]=4,[0x13fca]=2,[0x13fcc]=2,[0x13fce]=2,[0x13fd0]=2,[0x13fd2]=2,[0x13fd4]=2,[0x13fd6]=2,
 [0x13fd8]=4,[0x13fe6]=4,[0x13fea]=4,[0x13ffa]=2,[0x13ffc]=2,[0x13ffe]=2,[0x14000]=2,[0x14002]=2,
 [0x14004]=2,[0x14006]=2,[0x14008]=4,[0x14016]=4,[0x1401a]=4,[0x14028]=2,[0x1402a]=2,[0x1402c]=2,
 [0x1402e]=2,[0x14030]=2,[0x14032]=2,[0x14034]=2,[0x14036]=6,[0x14046]=4,[0x14054]=2,[0x14056]=2,
 [0x14058]=2,[0x1405a]=2,[0x1405c]=2,[0x1405e]=2,[0x14060]=2,[0x14062]=6,[0x1406e]=4,[0x14092]=4,
 [0x14096]=4,[0x140a4]=2,[0x140a8]=2,[0x140aa]=4,[0x140c2]=2,[0x140c4]=2,[0x140d4]=2,[0x140d6]=2,
 [0x142dc]=6,[0x142f2]=8,[0x14316]=4,[0x1431a]=2,[0x14326]=2,[0x14338]=2,[0x143c6]=2,[0x143c8]=2,
 [0x143ca]=4,[0x143de]=2,[0x143e0]=2,[0x143e2]=4,[0x143f6]=2,[0x143f8]=2,[0x14412]=6,[0x1441a]=8,
 [0x1442a]=2,[0x1443a]=6,[0x14460]=2,[0x14478]=2,[0x1447a]=2,[0x1447c]=2,[0x1447e]=2,[0x14480]=2,
 [0x14482]=2,[0x14484]=2,[0x14488]=6,[0x1448e]=2,[0x14490]=2,[0x14492]=2,[0x14494]=2,[0x14496]=2,
 [0x14498]=2,[0x1449a]=2,[0x14a86]=8,[0x14a90]=6,[0x14aa4]=6,[0x14aaa]=6,[0x14ab6]=6,[0x14ac4]=2,
 [0x14aca]=6,[0x14ad0]=6,[0x14ada]=6,[0x14b14]=2,[0x14b16]=2,[0x14b18]=2,[0x14b1a]=2,[0x14b1c]=2,
 [0x14b1e]=2,[0x14b20]=2,[0x14b2a]=6,[0x14b34]=6,[0x14b58]=2,[0x14b5a]=2,[0x14b5c]=2,[0x14b5e]=2,
 [0x14b60]=2,[0x14b62]=2,[0x14b64]=2,[0x14b88]=2,[0x14b8a]=2,[0x14b8c]=2,[0x14b8e]=2,[0x14b90]=2,
 [0x14b92]=2,[0x14b94]=2,[0x14b96]=2,[0x14ba0]=2,[0x14ba6]=6,[0x14bac]=6,[0x14bc0]=6,[0x14bd6]=2,
 [0x14c26]=6,[0x14c2e]=6,[0x16c8a]=6,[0x16c94]=6,[0x16c9e]=6,[0x16ca4]=4,[0x16ca8]=6,[0x16cae]=4,
 [0x16cb2]=4,[0x16cbe]=4,[0x16cd8]=4,[0x16ce0]=4,[0x16cec]=4,[0x16cf4]=4,[0x16cf8]=4,[0x16d1c]=4,
 [0x16d22]=6,[0x16d28]=4,[0x16d2c]=6,[0x16d3a]=4,[0x18444]=4,[0x1844c]=6,[0x1848e]=4,[0x184a0]=4,
 [0x184a8]=6,[0x184ca]=4,[0x184e6]=2,[0x184e8]=2,[0x184ea]=2,[0x184ec]=2,[0x184ee]=2,[0x184f0]=2,
 [0x184f2]=2,[0x184f4]=2,[0x184f6]=2,[0x184f8]=2,[0x184fa]=2,[0x18502]=4,[0x18506]=2,[0x18510]=2,
 [0x1851a]=4,[0x1852a]=2,[0x1853e]=4,[0x1854c]=4,[0x1857a]=4,[0x18582]=4,[0x18588]=4,[0x1858e]=4,
 [0x18594]=4,[0x1859a]=4,[0x1859e]=4,[0x185a8]=6,[0x185ae]=4,[0x185b2]=4,[0x185b6]=6,[0x185c2]=2,
 [0x185c6]=2,[0x185c8]=2,[0x185d2]=2,[0x185d4]=2,[0x185d6]=2,[0x185d8]=2,[0x185e0]=4,[0x185e6]=4,
 [0x185ec]=4,[0x185f0]=4,[0x185fa]=4,[0x185fe]=4,[0x18608]=4,[0x18616]=4,[0x1861a]=4,[0x18624]=4,
 [0x18632]=4,[0x18636]=4,[0x18640]=4,[0x1864e]=4,[0x18652]=4,[0x1865c]=4,[0x1866c]=2,[0x18672]=4,
 [0x18678]=4,[0x18680]=2,[0x18688]=2,[0x1868a]=2,[0x18690]=2,[0x186aa]=2,[0x186ac]=2,[0x186b2]=2,
 [0x1871a]=4,[0x1871e]=4,[0x1872a]=6,[0x18730]=6,[0x1874a]=2,[0x1874c]=2,[0x1874e]=2,[0x1876a]=6,
 [0x1878c]=2,[0x1878e]=2,[0x18790]=2,[0x18798]=8,[0x187a4]=6,[0x187b6]=2,[0x187c2]=4,[0x187d0]=4,
 [0x187d4]=2,[0x187d6]=2,[0x187d8]=2,[0x187e4]=6,[0x18800]=6,[0x1881e]=6,[0x18832]=2,[0x18834]=2,
 [0x18836]=2,[0x18848]=2,[0x1884a]=2,[0x1884c]=2,[0x18852]=8,[0x1885a]=6,[0x1887a]=2,[0x1887c]=2,
 [0x1887e]=4,[0x1888a]=2,[0x18898]=2,[0x188a0]=2,[0x18906]=2
}
-- Literal game-flow operand lengths, excluding ROM prefetches.
for p,length in pairs({
 [0xb840]=4,[0xc6fc]=6,[0xc702]=6,[0xc70a]=2,[0xc710]=2,[0xc71c]=4,[0xc73c]=4,[0xc740]=4,
 [0xc762]=2,[0xc768]=2,[0xc77e]=2,[0xc780]=8,[0xc78c]=4,[0xc794]=4,[0xc79c]=4,[0xc7a4]=2,
 [0xc7aa]=2,[0xc7ae]=4,[0xc7e2]=4,[0xc7ee]=4,[0xc7fa]=4,[0xc806]=4,[0xc812]=4,[0xc81e]=4,
 [0xc82a]=4,[0xc83a]=2,[0xc848]=4,[0xc852]=4,[0xc856]=2,[0xc85e]=4,[0xc866]=2,[0xc868]=2,
 [0xc86c]=2,[0xc86e]=2,[0xc876]=4,[0xc8b0]=4,[0xc8b4]=2,[0xc8b6]=6,[0xc8be]=4,[0xc8c6]=4,
 [0xc8ce]=4,[0xc8d8]=4,[0xc8dc]=4,[0xc8e4]=4,[0xc8ec]=4,[0xc8f0]=4,[0xc900]=4,[0xc90a]=4,
 [0xc910]=4,[0xc914]=2,[0xc916]=4,[0xc91c]=4,[0xc930]=2,[0xc934]=4,[0xc93c]=4,[0xc944]=4,
 [0xc950]=6,[0xc956]=4,[0xc95a]=4,[0xc968]=6,[0xc96e]=2,[0xc970]=4,[0xc974]=4,[0xc980]=4,
 [0xc984]=4,[0xc98a]=2,[0xc98c]=2,[0xc992]=4,[0xc99a]=4,[0xc9a6]=4,[0xc9aa]=2,[0xcf46]=6,
 [0xcf4c]=4,[0xcf54]=4,[0xcf5c]=4,[0xcf60]=4,[0xcf68]=4,[0xcf74]=4,[0xcf7a]=2,[0xcf80]=4,
 [0xcf88]=6,[0xcf98]=2,[0xcf9a]=4,[0xcfa6]=6,[0xcfb0]=4,[0xcfba]=4,[0x137e2]=2,[0x137e4]=2,
 [0x137e6]=2,[0x137f0]=2,[0x137f4]=4,[0x137f8]=2,[0x137fa]=2,[0x137fc]=2,[0x137fe]=4,[0x13804]=2,
 [0x13806]=2,[0x1380a]=2,[0x1380c]=2,[0x1380e]=2,[0x13812]=4,[0x13a32]=4,[0x13a36]=6,[0x13a3c]=2,
 [0x13a3e]=2,[0x13a40]=2,[0x13a42]=2,[0x13a44]=2,[0x13a46]=2,[0x13a48]=2,[0x13a4a]=2,[0x17146]=8,
 [0x17152]=8,[0x1718a]=6,[0x17194]=4,[0x1719c]=4,[0x171a8]=6,[0x171b6]=8,[0x171be]=4,[0x171c2]=4,
 [0x171c6]=4,[0x171cc]=4,[0x171d0]=4,[0x171d4]=2,[0x171d6]=2,[0x17200]=6,[0x1720a]=6,[0x17210]=6,
 [0x1721e]=4,[0x17234]=6,[0x17240]=6,[0x1724a]=6,[0x17252]=4,[0x1725a]=2,[0x1725c]=4,[0x17260]=4,
 [0x17270]=4,[0x17274]=4,[0x1727e]=4,[0x17282]=6,[0x17288]=4,[0x17296]=2,[0x17298]=6,[0x1729e]=6,
 [0x172a4]=2,[0x172aa]=6,[0x172b0]=4,[0x172c4]=4,[0x172cc]=4,[0x172d0]=2,[0x172d2]=4,[0x172de]=6,
 [0x172e4]=6,[0x172ea]=6,[0x17300]=4,[0x17304]=6,[0x1730a]=4,[0x1730e]=6,[0x17314]=4,[0x17318]=6,
 [0x17336]=6,[0x17340]=4,[0x17344]=6,[0x17354]=4,[0x17358]=2,[0x1735a]=6,[0x17360]=6,[0x17366]=6,
 [0x1736c]=6,[0x17372]=4,[0x17376]=2,[0x1737e]=4,[0x17382]=6,[0x1738c]=6,[0x17392]=2,[0x17394]=4,
 [0x17398]=4,[0x173a4]=2,[0x173a6]=6,[0x173b0]=4,[0x173b4]=4,[0x173b8]=4,[0x173c2]=2,[0x173c4]=4,
 [0x173c8]=2,[0x173ca]=2,[0x173cc]=4,[0x173d0]=6,[0x173d6]=6,[0x173e0]=2,[0x173e2]=6,[0x173ec]=6,
 [0x173f2]=6,[0x173fc]=2,[0x173fe]=6,[0x17408]=4,[0x17410]=6,[0x17416]=6,[0x17426]=6,[0x17438]=2,
 [0x1743a]=6,[0x17444]=4,[0x17452]=6,[0x17458]=6,[0x1745e]=6,[0x17464]=6,[0x1746a]=4,[0x17474]=4,
 [0x17480]=4,[0x17484]=4,[0x1748c]=4,[0x17490]=2,[0x17492]=4,[0x1749e]=6,[0x174a4]=4,[0x174a8]=6,
 [0x174b0]=4,[0x174b4]=4,[0x174bc]=4,[0x174c8]=4,[0x174cc]=4,[0x174d4]=4,[0x174dc]=4,[0x174e0]=2,
 [0x174e2]=4,[0x174e6]=4,[0x174ee]=4,[0x174fc]=2,[0x17504]=2,[0x17506]=4,[0x1750e]=4,[0x1751c]=6,
 [0x17528]=4,[0x1752c]=4,[0x17534]=6,[0x1753a]=4,[0x17544]=2,[0x17546]=4,[0x1754c]=4,[0x17550]=2,
 [0x17552]=2,[0x1757c]=2,[0x17586]=2,[0x17590]=2,[0x1759a]=2,[0x175a0]=6,[0x175ac]=6,[0x175b2]=4,
 [0x175bc]=4,[0x175c0]=4,[0x175c4]=2,[0x175c6]=4,[0x175d4]=4,[0x175d8]=4,[0x175e4]=2,[0x175e6]=2,
 [0x175e8]=4,[0x175f4]=6,[0x175fa]=4,[0x175fe]=6,[0x17610]=4,[0x17614]=2,[0x17624]=4,[0x17632]=4,
 [0x17636]=4,[0x1763e]=2,[0x17640]=2,[0x17642]=2,[0x17644]=2,[0x17646]=2,[0x1764c]=2,[0x1764e]=6,
 [0x17658]=2,[0x1765e]=2,[0x17662]=2,[0x17664]=2,[0x17666]=2,[0x17668]=6,[0x1766e]=6,[0x17674]=6,
 [0x1767e]=2,[0x17680]=2,[0x17682]=4,[0x17686]=4,[0x1768e]=4,[0x17696]=4,[0x1769e]=4,[0x176a6]=4,
 [0x176ae]=4,[0x176b6]=4,[0x176be]=4,[0x176c6]=4,[0x176ce]=4,[0x176d6]=4,[0x176dc]=4,[0x176e0]=4,
 [0x176e4]=4,[0x176ee]=2,[0x176f4]=4,[0x176fc]=4,[0x17700]=4,[0x1770a]=4,[0x17710]=2,[0x17712]=4,
 [0x1771a]=4,[0x17720]=4,[0x1772a]=6,[0x17730]=2,[0x17732]=6,[0x17744]=4,[0x17748]=4,[0x17758]=2,
 [0x1775e]=2,[0x17764]=4,[0x17776]=4,[0x1777a]=4,[0x17784]=4,[0x17790]=2,[0x17792]=2,[0x177a6]=4,
 [0x177aa]=4,[0x177b6]=2,[0x177b8]=4,[0x177ce]=4,[0x177d2]=4,[0x177dc]=4,[0x177f8]=6,[0x177fe]=4,
 [0x17802]=2,[0x17810]=6,[0x17816]=4,[0x1781a]=2,[0x17822]=4,[0x17826]=4,[0x1782a]=4,[0x17832]=4,
 [0x17870]=4,[0x17886]=6,[0x17894]=2,[0x17896]=6,[0x178a0]=6,[0x178aa]=6,[0x178b4]=2,[0x178b6]=4,
 [0x178ba]=4,[0x178be]=2,[0x178c0]=2,[0x178c6]=2,[0x178c8]=4,[0x178cc]=4,[0x178d0]=2,[0x178d2]=2,
 [0x178de]=2,[0x178e0]=4,[0x178e4]=2,[0x178e6]=6,[0x178f4]=4,[0x178f8]=4,[0x178fc]=6,[0x1790a]=6,
 [0x17918]=2,[0x17920]=4,[0x17924]=4,[0x17928]=4,[0x17930]=4,[0x17934]=4,[0x1793c]=4,[0x17944]=4,
 [0x1794c]=4,[0x17954]=4,[0x1795c]=2,[0x1795e]=4,[0x17962]=4,[0x17966]=4,[0x1796a]=2,[0x17970]=2,
 [0x17972]=4,[0x17976]=4,[0x1797a]=4,[0x1797e]=4,[0x17982]=2,[0x17988]=2,[0x1798a]=2,[0x1798c]=4,
 [0x17994]=2,[0x17996]=4,[0x1799a]=4,[0x179a2]=2,[0x179a4]=4,[0x179a8]=4,[0x179b0]=2,[0x179b2]=4,
 [0x179b6]=4,[0x179ba]=4,[0x179be]=6,[0x179c6]=6,[0x179d0]=4,[0x179d4]=4,[0x179d8]=6,[0x179ea]=4,
 [0x179ee]=2,[0x179f4]=4,[0x179f8]=6,[0x179fe]=4,[0x17a02]=6,[0x17a08]=4,[0x17a0c]=4,[0x17a14]=4,
 [0x17a42]=4,[0x17a46]=6,[0x17a4c]=4,[0x17a50]=6,[0x17a56]=4,[0x17a5c]=4,[0x17a60]=6,[0x17a66]=4,
 [0x17a6a]=6,[0x17a70]=4,[0x17a76]=2,[0x17a7c]=4,[0x17a80]=6,[0x17a88]=4,[0x17a8c]=4,[0x17a90]=4,
 [0x17a94]=4,[0x17a98]=4,[0x17a9c]=2,[0x17a9e]=4,[0x17aa2]=4,[0x17ab2]=4,[0x17ac8]=4,[0x17acc]=4,
 [0x17ad8]=4,[0x17ae0]=4,[0x17ae4]=4,[0x17aea]=4,[0x17af2]=2,[0x17af6]=2,[0x17af8]=4,[0x17afc]=8,
 [0x17b08]=8,[0x17b14]=2,[0x17b16]=4,[0x17b20]=2,[0x17b22]=4,[0x17b26]=4,[0x17b2c]=2,[0x17b2e]=4,
 [0x17b32]=2,[0x17b34]=6,[0x17b3a]=6,[0x17b40]=6,[0x17b48]=4,[0x17b4e]=4,[0x17b56]=2,[0x17b58]=4,
 [0x17b5c]=2,[0x17b5e]=2,[0x17b60]=6,[0x17b66]=6,[0x17b74]=4,[0x17b78]=4,[0x17b7c]=2,[0x17b7e]=4,
 [0x17b82]=4,[0x17b86]=4,[0x17b8a]=4,[0x17b8e]=2,[0x17b90]=4,[0x17b98]=6,[0x17ba0]=6,[0x17ba6]=6,
 [0x17bac]=4,[0x17bb4]=6,[0x17bba]=6,[0x17bc2]=4,[0x17bc6]=4,[0x17bcc]=4,[0x17bd2]=2,[0x17bd4]=4,
 [0x17bda]=4,[0x17be2]=4,[0x17bea]=4,[0x17bee]=6,[0x17bf6]=4,[0x17c04]=6,[0x17c22]=6,[0x17c28]=6,
 [0x17c2e]=6,[0x17c34]=6,[0x17c3a]=4,[0x17c3e]=6,[0x17c44]=6,[0x17c4a]=6,[0x17c50]=6,[0x17c56]=4,
 [0x17c5a]=6,[0x17c7c]=4,[0x17c84]=2,[0x17c86]=4,[0x17c8e]=4,[0x17c92]=4,[0x17c96]=6,[0x17ca0]=6,
 [0x17cb4]=2,[0x17cb6]=2,[0x17cb8]=4,[0x17cc0]=4,[0x17cc4]=2,[0x17cdc]=2,[0x17cde]=2,[0x17ce4]=4,
 [0x17ce8]=2,[0x17cec]=2,[0x17cee]=2,[0x17cf4]=4,[0x17cf8]=2,[0x17cfc]=2,[0x17cfe]=2,[0x17d04]=4,
 [0x17d08]=2,[0x17d0e]=2,[0x17d14]=4,[0x17d3a]=6,[0x17d48]=4,[0x17d50]=2,[0x17d56]=4,[0x17d5a]=4,
 [0x17d5e]=2,[0x17d6c]=4,[0x17d70]=4,[0x17d74]=2,[0x17d76]=2,[0x17d78]=2,[0x17d7a]=2,[0x17d7c]=4,
 [0x17d82]=2,[0x17d86]=2,[0x17d88]=4,[0x17d8e]=2,[0x17d90]=2,[0x17d92]=2,[0x17d94]=4,[0x17d9a]=2,
 [0x17d9e]=2,[0x17da0]=4,[0x17da4]=2,[0x17da6]=4,[0x17dac]=2,[0x17db2]=2,[0x17db6]=2,[0x17dbc]=2,
 [0x17dc0]=2,[0x17dc6]=2,[0x17dd8]=2,[0x17dda]=2,[0x17dea]=4,[0x17dee]=4,[0x17df2]=2,[0x17df4]=2,
 [0x17df8]=2,[0x17dfa]=4,[0x17e00]=2,[0x17e04]=2,[0x17e06]=4,[0x17e0c]=2,[0x17e0e]=2,[0x17e10]=4,
 [0x17e16]=2,[0x17e1a]=2,[0x17e1c]=4,[0x17e20]=2,[0x17e24]=2,[0x17e26]=4,[0x17e2c]=2,[0x17e30]=2,
 [0x17e32]=4,[0x17e38]=2,[0x17e3a]=2,[0x17e3c]=4,[0x17e42]=2,[0x17e46]=2,[0x17e48]=4,[0x17e4c]=2,
 [0x17e4e]=4,[0x17e54]=2,[0x17e5a]=2,[0x17e5e]=2,[0x17e64]=2,[0x17e68]=2,[0x17e6e]=2,[0x17e72]=2,
 [0x17e78]=2,[0x17e7c]=2,[0x17e82]=2,[0x17e86]=2,[0x17e8c]=2,[0x17e90]=2,[0x17e96]=2,[0x17eae]=2,
 [0x17eb0]=2,[0x17eb2]=2,[0x17eb4]=2,[0x17ec0]=4,[0x17ec4]=2,[0x17ec6]=2,[0x17eca]=2,[0x17ecc]=4,
 [0x17ed2]=2,[0x17ed4]=2,[0x17ed8]=2,[0x17eda]=4,[0x17ee0]=2,[0x17ee2]=2,[0x17ee4]=2,[0x17ee6]=4,
 [0x17eec]=2,[0x17eee]=2,[0x17ef2]=2,[0x17ef4]=4,[0x17ef8]=2,[0x17efa]=2,[0x17efe]=2,[0x17f00]=4,
 [0x17f06]=2,[0x17f08]=2,[0x17f0c]=2,[0x17f0e]=4,[0x17f14]=2,[0x17f16]=2,[0x17f18]=2,[0x17f1a]=4,
 [0x17f20]=2,[0x17f22]=2,[0x17f26]=2,[0x17f28]=4,[0x17f2c]=2,[0x17f2e]=2,[0x17f30]=4,[0x17f34]=4,
 [0x17f3a]=2,[0x17f40]=2,[0x17f44]=2,[0x17f4a]=2,[0x17f4e]=2,[0x17f54]=2,[0x17f58]=2,[0x17f5e]=2,
 [0x17f62]=2,[0x17f68]=2,[0x17f6c]=2,[0x17f72]=2,[0x17f76]=2,[0x17f7c]=2,[0x17f96]=2,[0x17f98]=2,
 [0x17f9a]=2,[0x17f9c]=2,[0x17fa0]=6,[0x17fa6]=6,[0x17fac]=6,[0x17fb2]=6,[0x17fb8]=2,[0x17fba]=2,
 [0x17fbc]=2,[0x17fbe]=2,[0x17fc6]=2,[0x17fc8]=6,[0x17fce]=4,[0x17fd2]=4,[0x17fd8]=2,[0x17fda]=2,
 [0x17fde]=4,[0x17fe2]=4,[0x17fe8]=2,[0x17fea]=2,[0x17fee]=4,[0x17ff2]=4,[0x17ff8]=2,[0x17ffc]=2,
 [0x17ffe]=2,[0x18000]=2,[0x18002]=2,[0x18008]=4,[0x18012]=4,[0x18020]=8,[0x18030]=2,[0x18032]=2,
 [0x18034]=2,[0x18036]=2,[0x18038]=4,[0x1803c]=2,[0x18046]=4,[0x18050]=2,[0x18052]=2,[0x18054]=8,
 [0x18064]=2,[0x18066]=2,[0x18068]=4,[0x1806c]=2,[0x1807a]=4,[0x1807e]=4,[0x180a2]=8,[0x180aa]=8,
 [0x180b2]=8,[0x180ba]=8,[0x180c2]=8,[0x180ca]=8,[0x180d2]=6,[0x1810c]=6,[0x18116]=2,[0x18118]=4,
 [0x18120]=6,[0x1812a]=6,[0x18138]=2,[0x1813a]=4,[0x18144]=4,[0x1814a]=4,[0x1814e]=2,[0x18150]=4,
 [0x18154]=4,[0x1815a]=4,[0x1815e]=4,[0x18162]=2,[0x18164]=4,[0x18168]=4,[0x1816e]=4,[0x18174]=6,
 [0x18206]=6,[0x18210]=4,[0x18214]=4,[0x18268]=6,[0x1826e]=6,[0x18274]=6,[0x1827a]=4,[0x18284]=4,
 [0x1828c]=4,[0x18290]=2,[0x18292]=2,[0x18294]=2,[0x1829a]=2,[0x1829e]=4,[0x182a8]=2,[0x182aa]=4,
 [0x182b0]=4,[0x182b6]=6,[0x18300]=4,[0x18304]=2,[0x18308]=2,[0x1830c]=2,[0x18310]=2,[0x18314]=2,
 [0x18318]=2,[0x1832a]=4,[0x1832e]=2,[0x18332]=2,[0x1838c]=4,[0x18392]=4,[0x18396]=4,[0x1839c]=4,
 [0x183a0]=6,[0x183b8]=2,[0x183bc]=2,[0x183be]=2,[0x183c2]=4,[0x183c6]=2,[0x183c8]=4,[0x183d8]=4,
}) do operand_lengths[p]=length end
local targets={
 [0x146c6]={0x14a86,4},[0x146ca]={0x13f92,4},[0x146d8]={0x13bcc,4},
 [0x146e6]={0x13bb6,4},[0x146f4]={0x13bfa,4},[0x14702]={0x1329e,4},
 [0x14706]={0x16c8a,6},[0x1470c]={0x18444,6},[0x14712]={0x1495c,4},
 [0x14720]={0x148be,4},[0x14724]={0x17146,4},[0x14728]={0x13e5c,4},
 [0x1473e]={0xb7d2,6},[0x1475a]={0x148be,4},[0x1475e]={0x1495c,4},
 [0x1476c]={0x14a86,4},[0x14770]={0x13f92,4},[0x14774]={0x18444,6},
 [0x1477a]={0x17146,4},[0x1477e]={0x13e5c,4},[0x1478a]={0xb7c2,6},
 [0x147ac]={0x148b8,4},[0x147b0]={0x14990,4},[0x147b4]={0xc6f8,6},
 [0x147ba]={0xe2da,4},[0x147be]={0x147e8,4}
}
local function ns()return math.floor(manager.machine.time:as_double()*1e9+0.5)end
local function regs()
 local r={};for i=0,7 do r[#r+1]=string.pack(">I4",main.state["D"..i].value&0xffffffff)end
 for i=0,6 do r[#r+1]=string.pack(">I4",main.state["A"..i].value&0xffffffff)end
 return table.concat(r)
end
local function snapshot()
 guard=true;local parts={}
 for _,r in ipairs({{0x80000,0x83fff},{0xa0000,0xa3fff},{0x280000,0x283fff},{0x2a0000,0x2a3fff},{0xc0000,0xcffff},{0xd0000,0xd0fff},{0x100000,0x100fff},{0x120000,0x123fff},{0x2ec000,0x2ecfff}})do parts[#parts+1]=space:read_range(r[1],r[2],8)end
 guard=false;return table.concat(parts)
end
local function access(w,a,d,m)
 if guard then return end
 local p=main.state.CURPC.value
 if return_pending then
  return_pending=false
  local irq=w and a==((main.state.SP.value-2)&0x3ffffe) and (main.state.SR.value&0x700)>0x100
  returns:write(string.char(irq and (main.state.SR.value>>8&7) or 0))
  if irq then
   local selected=main.state.SR.value>>8&7
   return_entry={events={},target=selected==2 and 0x1461c or selected==4 and 0x14674 or 0x1466c}
  end
 end
 if return_entry then
  if w or a<0x80 then return_entry.events[#return_entry.events+1]=string.pack(">I8I4I4I2I2B",ns(),p,a,d,m,w and 1 or 0)end
  if not w and a==return_entry.target+2 then
   returns:write(string.pack(">I8I4",ns()+320,#return_entry.events),table.concat(return_entry.events),
    string.pack(">I4I2",main.state.SP.value,main.state.SR.value),regs())
   return_entry=nil
  end
 end
 if not active and w and last and blank:read(0)~=0 and main.state.SR.value>>8&7==4 and a==((main.state.SP.value-2)&0x3ffffe) then
  if directed then
   guard=true
   space:write_u16(0x2a3c00,({0,1,0x8000,0xffff})[calls%4+1])
   -- Ordinary directed runs skip credit callbacks; the RTS fixture enters
   -- them while preserving the existing final service redirect.
   space:write_u8(0x3f8050,calls==limit-5 and 0xff or (os.getenv("AB2_ORACLE_IRQ4_RTS_CALLEES")=="1" and 0 or 1))
   space:write_u8(0x3f913d,3);space:write_u8(0x3f913e,1);space:write_u8(0x3f9140,1)
   -- Directed operand fixtures, not gameplay/stage-selection claims.
   local road_case=calls//4
   space:write_u32(0x3f9142,0x30cf4+((road_case&7)<<4))
   space:write_u8(0x3f9115,road_case&0xff)
   space:write_u8(0x3f9114,road_case&0xff)
   space:write_u16(0x29c060,road_case%26==25 and 0xffff or road_case%26)
   if os.getenv("AB2_ORACLE_IRQ4_ROAD_ENDPOINTS")=="1" then
    space:write_u8(0x3f9115,({0,1,0xfe,0xff})[road_case%4+1])
    space:write_u8(0x3f9114,0xf8|(road_case&7))
    space:write_u16(0x29c060,({0,24,0xffff,0x8000})[road_case%4+1])
   end
   if os.getenv("AB2_ORACLE_IRQ4_CYCLE_ENDPOINTS")=="1" then
    -- Cross unsigned selector boundaries, phase masking/word wrap and
    -- both hardware/fade destinations. Earlier fade executes normally.
    space:write_u16(0x29c09c,({0x11,0x12,0x16,0x17,0x10,0xffff,0,0x8000})[road_case%8+1])
    space:write_u16(0x3f9146,({0,1,0x0f,0x10,0x1f,0x20,0x7fff,0x8000,0xffff})[(road_case//8)%9+1])
    space:write_u8(0xcdd07,0);space:write_u16(0xcdd02,0)
    space:write_u16(0xcdd00,0);space:write_u8(0xcdd05,(road_case//72)%2==0 and 0 or 2)
   end
   if os.getenv("AB2_ORACLE_IRQ4_CALLEE_ENDPOINTS")=="1" then
    local k=calls//4
    local stages={15,16,5,13,20,1,7,9}
    space:write_u16(0x29c09c,stages[k%8+1])
    -- Fade trigger/capture, all upload banks, all three channel masks,
    -- inactive animation, blend setup/step and transition completion.
    local fade=k%4==0
    space:write_u8(0xcdd07,fade and k%12==0 and 1 or 0)
    space:write_u8(0xcdd04,k&31);space:write_u8(0xcdd06,(k//4)&7)
    space:write_u16(0xcdd02,fade and (k//4)%5==0 and (1+(k//20)%3) or 0)
    space:write_u16(0xcdd00,(k//4)&7)
    space:write_u8(0xcdd05,fade and 2 or 0)
    space:write_u8(0xcdd08,({0,1,2,2})[k%4+1]);space:write_u8(0xcdd09,k%8<4 and 1 or 0x40)
    space:write_u16(0x3f8c2a,({0,4,30,31})[k%4+1])
    space:write_u16(0x3f8d30,0);space:write_u16(0x3f8b5a,k&7)
    space:write_u16(0x29c0d2,k%9==0 and 0xffff or (k%32)<<4)
    space:write_u8(0x29c54c,k%6==0 and 0 or 2)
    space:write_u8(0x3f8c2e,k&1);space:write_u8(0x3f8c2c,k%8)
    -- Reload request priorities, signed delay, exhaustion, sound disable
    -- and wrap, decimal tens/hundreds and HUD clamp.
    space:write_u8(0x3f9102,({0,1,2,3})[k%4+1])
    space:write_u8(0x3f90ff,k%7==0 and 0 or 255)
    space:write_u8(0x3f9103,({1,0,0xe0,0x7f})[(k//4)%4+1])
    space:write_u16(0x3f9100,({2,0,0xffff,15})[(k//16)%4+1])
    space:write_u16(0x3f8d66,({0,1,9,10,19,99,100,139,140,0x7fff})[k%10+1])
    space:write_u16(0x3f9106,({0,3,0xffff,0x7fff})[(k//4)%4+1])
    space:write_u8(0x3f8902,k%5==0 and 0 or 1);space:write_u16(0x3f8908,k%2==0 and 63 or 0)
    -- Scenery visibility entry/exit, segmented display and tile clear.
    -- Refuel-mode stages retain their ROM-initialized event records.
    space:write_u16(0x3f9044,k%13==0 and 7 or 0)
    space:write_u16(0x3f9046,k%(8*(space:read_u16(0x188ca)+1)))
    space:write_u16(0x3f9048,k%3==0 and 0x4000 or 0)
    local flags=space:read_u16(0x29c200)
    space:write_u16(0x29c200,k%3==1 and (flags|0x4000) or (flags&~0x4000))
   end
   if os.getenv("AB2_ORACLE_IRQ4_SCENERY_ENDPOINTS")=="1" then
    local k=calls//4
    space:write_u16(0x29c09c,5);space:write_u8(0xcdd05,0)
    space:write_u16(0xcdd02,0);space:write_u8(0xcdd07,0)
    space:write_u16(0x3f9044,k%23==0 and 7 or 0)
    space:write_u16(0x3f9040,(k//2)%16)
    space:write_u8(0x3f9035,0xff)
    if k%2==1 then
     -- Already initialized event: timed scroll modes/underflow and ROM
     -- palette-script loops. Source pointers remain ROM-selected.
     space:write_u16(0x3f9040,0x100)
     space:write_u8(0x3f9000,({0,4,8,12,16,20,24,28})[(k//2)%8+1])
     space:write_u16(0x3f9002,k%3==0 and 0 or 1)
     space:write_u16(0x3f9004,k%3==0 and 0 or 1)
     space:write_u16(0x3f9030,0);space:write_u8(0x3f9034,k%3==0 and 0 or 1)
     space:write_u8(0x3f9001,k%3==0 and 0 or 1)
     local record=({0,1,3,4})[(k//16)%4+1]
     space:write_u32(0x3f9020,space:read_u32(0x220004+record*16))
     space:write_u16(0x3f9032,0)
    end
   end
   if (os.getenv("AB2_ORACLE_IRQ4_GAMEFLOW_ENDPOINTS")=="1" or os.getenv("AB2_ORACLE_IRQ4_GAMEFLOW_ACTIONS")=="1") then
    -- Isolated ROM-defined state-machine/coin operands. These are explicit
    -- fixtures, not a claim that earlier credit/attract callers produced them.
    local k=calls
    space:write_u8(0x3f8050,0)
    space:write_u16(0x29c09c,k%2==0 and 26 or 3)
    for a=0x3f8400,0x3f8583 do space:write_u8(a,0) end
    for i=0,279 do space:write_u8(0x3f846e+i,space:read_u8(0x31116+i)) end
    local rank=({0,3,17,19})[(k//16)%4+1]
    space:write_u8(0x3f8414,(k//2)%8);space:write_u8(0x3f8416,rank)
    space:write_u8(0x3f840c,({0,4,8})[(k//64)%3+1])
    space:write_u16(0x3f8426,({0,1,13})[(k//16)%3+1])
    space:write_u32(0x3f8884,({0,0x12345678,0x99999999})[(k//8)%3+1])
    space:write_u16(0x29c0b8,10);space:write_u16(0x29c0bc,0)
    space:write_u16(0x29c0ba,({0,10,54,109,164,219})[(k//32)%6+1])
    local text=rank<3 and (0xd0428+rank*256) or rank<17 and 0xd0728 or (0xd0a28+(rank-19)*256)
    space:write_u32(0x3f842e,text);space:write_u32(0x3f842a,text+42)
    space:write_u32(0x3f8434,text+14)
    space:write_u8(0x3f8428,({0,1,39,45})[(k//16)%4+1])
    space:write_u8(0x3f8420,({0,9,30})[(k//16)%3+1])
    space:write_u8(0x3f8421,k%2==0 and 40 or 0)
    space:write_u8(0x3f8422,k&0x18)
    space:write_u8(0x3f8438,({0,8,16,24,40})[(k//16)%5+1])
    space:write_u8(0x3f843b,k%3==0 and 1 or 0)
    space:write_u8(0x3f843a,k%2)
    -- Physical coin edges, every ROM coinage selector, ordinary/extended
    -- BCD credit limits and lamp countdown/release paths.
    local buttons=manager.machine.ioport.ports[":mainpcb:IO1PORTA"].fields
    buttons["Coin 1"]:set_value(k%4<2 and 1 or 0)
    buttons["Coin 2"]:set_value((k%4==0 or k%4==3) and 1 or 0)
    space:write_u8(0x3f8070,({0,8,16,24})[(k//16)%4+1])
    space:write_u8(0x3f8052,({0,8,9,0x98,0x99})[(k//8)%5+1])
    space:write_u8(0x3f8054,(k//4)%15);space:write_u8(0x3f8059,(k//8)%15)
    for _,a in ipairs({0x3f8056,0x3f8057,0x3f805b,0x3f805c}) do space:write_u8(a,({0,1,6})[(k//16)%3+1]) end
    if os.getenv("AB2_ORACLE_IRQ4_GAMEFLOW_ACTIONS")=="1" then
     -- Input-driven editing/removal, secret-name transitions and extended
     -- credits during the ROM's lives/HUD stages. Initial operands remain
     -- labelled fixture inputs; every resulting write is real CPU execution.
     local stage=k%16<12 and 26 or (15+k%4)
     space:write_u16(0x29c09c,stage)
     space:write_u8(0x3f8052,stage==26 and 0 or 0x98)
     space:write_u8(0x3f8414,k%8);space:write_u8(0x3f8415,k%3==0 and 1 or 0)
     space:write_u8(0x3f8429,k&1)
     space:write_u8(0x3f8428,({6,19,22,23,24,25,29,33,39,45})[(k//8)%10+1])
     space:write_u8(0x3f8438,({0,8,16,24,32,40})[(k//16)%6+1])
     space:write_u8(0x3f843a,1);space:write_u8(0x3f843b,0)
     space:write_u16(0x3f8418,({0,1,27,28})[(k//8)%4+1])
     space:write_u16(0x3f841e,59)
     space:write_u8(0x3f8892,({0x20,0x70,0x80,0x90,0xe0})[(k//8)%5+1])
     space:write_u8(0x3f8420,0);space:write_u8(0x3f8421,40)
     space:write_u32(0x3f846e+14*rank+4,({0x53455820,0x4b455920,0x542e4b20,0x48495220})[(k//8)%4+1])
     space:write_u8(0x3f913f,1)
     space:write_u16(0x3f88e0,({0,1,2,30,31})[(k//16)%5+1])
     space:write_u16(0x3f9124,({0,8})[(k//16)%2+1])
     buttons["Vulcan"]:set_value(k%4<2 and 1 or 0)
     assert(buttons["Service 1"]):set_value(k%8<4 and 1 or 0)
     space:write_u32(0x3f8812,k%7==0 and 0xffffffff or 0)
    end
   end
   space:write_u8(0x3f8b6f,calls%8==3 and (space:read_u8(0x3f8b6f)&~2) or (space:read_u8(0x3f8b6f)|2))
   space:write_u16(0x3f8174,59);space:write_u16(0x3f881c,59);space:write_u16(0x3f880a,1)
   guard=false
  end
  initial_usp=main.state.USP.value
  pre=string.pack(">I8I4I4I2I4I2",last.t+320,last.pc,main.state.PC.value,(last.sr&0xffe0)|(main.state.SR.value&31),main.state.SP.value,last.count)..regs()..snapshot()
  active=true;events={};hooks={};subevents={};window_gameflow_pcs={};operand_frontier=0;hook=nil;service_return=main.state.PC.value
 end
 if active then
  if hook and (hook.entry==0x17146 or hook.entry==0xc6f8) then window_gameflow_pcs[p]=true end
  local target=targets[p]
  if not w and target and a==target[1]+2 then
   assert(not hook,"nested root callback")
   hook={entry=target[1],ret=p+target[2],start=ns()+320,pre=regs(),events={}}
  end
  -- Data/stack and vector transactions are included. ROM instruction
  -- fetches are represented by clocks. External callees remain transcripts.
  -- Include road/cycle palette ROM operands. These MOVE instructions have no
  -- extensions; their prefetch is at p+4. Data ranges in these fixtures
  -- are disjoint from the instruction stream (also asserted by the test).
  local palette_read=(p==0x13bc4 or (p>=0x13bf0 and p<=0x13bf6) or p==0x13c12 or (p>=0x13c28 and p<=0x13c36) or (p>=0x132da and p<=0x132e6) or (p>=0x13310 and p<=0x1331c)) and a~=p+4
  local length=operand_lengths[p]
  local callee_read=hook and length and not(a>=p+2 and a<=p+length+2)
  if w or a>=0x80000 or a==0x70 or a==0x72 or palette_read or callee_read then
   local e=string.pack(">I8I4I4I2I2B",ns(),p,a,d,m,w and 1 or 0)
   if hook then hook.events[#hook.events+1]=e else events[#events+1]=e end
   operand_frontier=operand_frontier+1
  end
  if hook and not w and a==hook.ret+2 and p>=0x14674 and p<=0x147e6 then
   -- CURPC can already identify the caller on return prefetch.
   error("unexpected caller CURPC on callback return")
  end
  if hook and not w and a==hook.ret+2 and not targets[p] and not(p>=0x14674 and p<=0x147e6) then
   hooks[#hooks+1]=string.pack(">I4I8I8I4",hook.entry,hook.start,ns()+320,#hook.events)..hook.pre..regs()..table.concat(hook.events);hook=nil
  end
  if not w and (p==0x147c6 or p==0x147e6) and a==((p==0x147e6 and 0x1a132 or service_return+2)&0x3ffffe) then
   assert(not hook,"unfinished callback")
   for pc in pairs(window_gameflow_pcs) do gameflow_pcs[pc]=true end
   out:write(pre,string.pack(">I8I4I4I4",ns()+320,#events,#hooks,#subevents),table.concat(subevents),table.concat(events),table.concat(hooks),regs(),string.pack(">I4I2",main.state.SP.value,main.state.SR.value))
   if returns then
    returns:write(string.pack(">I4I8BI4I4",initial_usp,ns(),(timer:read(0)~=0 and 2 or 0)|(blank:read(0)~=0 and 4 or 0),
     main.state.SR.value&0x2000~=0 and main.state.SP.value or main.state.USP.value,main.state.PC.value))
    return_pending=true
   end
   active=false;calls=calls+1
  end
 end
 if not active then
  guard=true;local op=space:read_u16(p);guard=false
  last={t=ns(),pc=p,sr=main.state.SR.value,count=main.state["D"..(op>>9&7)].value&0xffff}
 end
end
irq4_reads=space:install_read_tap(0,0x3fffff,"irq4_reads",function(a,d,m)access(false,a,d,m)end)
irq4_writes=space:install_write_tap(0,0x3fffff,"irq4_writes",function(a,d,m)access(true,a,d,m)end)
-- Sub progress remains supplied at captured main operand frontiers for sub-owned
-- shared/road write lanes; no main read values or callee clocks are supplied.
local sub=manager.machine.devices[":mainpcb:subcpu"]
irq4_subwrites=sub.spaces.program:install_write_tap(0x80000,0xeffff,"irq4_subwrites",function(a,d,m)
 if guard or not active then return end
 if (a>=0x80000 and a<0xc0000) or (a>=0xec000 and a<0xed000) then
  subevents[#subevents+1]=string.pack(">I4I8I4I4I2I2B",operand_frontier,ns(),sub.state.CURPC.value,a+0x200000,d,m,1)
 end
end)
dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")
irq4_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1
 if frame>=tonumber(os.getenv("AB2_ORACLE_PASSES") or "600") then
  if active then dropped=dropped+1 end
  if returns then
   assert(not return_pending and not return_entry,"unfinished IRQ4 return assertion")
   returns:close()
  end
  out:close();local coverage=assert(io.open(os.getenv("AB2_ORACLE_OUT")..".gameflow-coverage","w"))
 local pcs={};for p in pairs(gameflow_pcs) do pcs[#pcs+1]=p end;table.sort(pcs)
 for _,p in ipairs(pcs) do coverage:write(string.format("%06x\n",p)) end;coverage:close()
 print(string.format("captured %d IRQ4 service envelopes through frame %d (%d unfinished)",calls,frame,dropped));manager.machine:exit()
 end
end)
