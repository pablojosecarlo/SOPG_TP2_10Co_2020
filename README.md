# SOPG_TP2_10Co_2020

/*
 * SOPG TP2 10Co 2020
 * Pablo J.C. Alonsp Castillo
 * 19/IV/2020
 * 
 * Serial service - tres luces
 * 
 * control de apertura de puerto tcp
 * control de reconexión del cliente tcp
 * control de broken pipe 
 * 
 * sigint sigterm - cierre controlado de los threads
 * sigpipe - finalmente no se usó.
 * 
 * Si no hay conecciones activas del server, el puerto 
 * serie hace un loop cerrado y devuelve las >SW:x,y como >COM:x,y
 * de modo que las luces se controlan desde la misma botonera
 * 
 * implementado en 5 threads. . 
 * 
 * NOTA: El Main.py se manda de las suyas . . . .  
 * Muchas veces crei que era mi programa, pero era el cliente
 * TCP el que decia una cosa y hacia otra, sobre todo cuando se 
 * lo abusa un poco con el ctrl+C para probar la resiliencia 
 * de nuestro soft. Esto me hizo pasar muchos sustos!! 
 * 
 */
