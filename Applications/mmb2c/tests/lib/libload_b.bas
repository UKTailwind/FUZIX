' tests/libload.bas's second library file, which follows the first.
' Its last line has no newline.
PRINT "library b runs next"

SUB greet(who$)
  PRINT "hello "; who$
END SUB

FUNCTION shout$(s$)
  shout$ = UCASE$(s$) + "!"
END FUNCTION