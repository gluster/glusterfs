(display (string-append "data-read" 
			(gfysh-read (gfysh-open (gfysh-init "/home/benkicode/volume.spec") 
					       "/scrap/optparser-test.py") 
				    1000
				    0)
			"end-read-data"))